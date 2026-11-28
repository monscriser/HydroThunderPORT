// LAN play: the in-game name and join-by-IP screens (see ht_lan_ui.h).
#include "ht_lan_ui.h"

#include "ht_config_edit.h"
#include "ht_lan_transport.h"
#include "ht_player_names.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/ui/imgui_dialog.h>

namespace ht::lan {
namespace {

using Clock = std::chrono::steady_clock;

// XINPUT_GAMEPAD buttons.
constexpr uint16_t kUp = 0x0001, kDown = 0x0002, kLeft = 0x0004, kRight = 0x0008;
constexpr uint16_t kStart = 0x0010, kBack = 0x0020;
constexpr uint16_t kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
constexpr auto kHold = std::chrono::milliseconds(900);

enum class Mode { kNone, kName, kAddress };

// Shared between the game's input polling (FilterPad) and the overlay (UI thread).
struct State {
  std::mutex mutex;
  Mode mode = Mode::kNone;
  uint32_t user = 0;               // whose name is being edited
  std::deque<uint16_t> presses;    // button presses for the open screen
  uint16_t previous[4] = {};
  bool swallow[4] = {};            // keep a pad idle until released
  Clock::time_point held_since[4][2] = {};  // [user][0 = Y, 1 = Back]
  bool just_opened = false;
};
State& Shared() {
  static State& s = *new State;  // never destroyed: polled until the process ends
  return s;
}

uint16_t Be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
void Put16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8), p[1] = uint8_t(v); }

}  // namespace

void FilterPad(uint32_t user, uint8_t* gamepad) {
  auto& s = Shared();
  std::lock_guard<std::mutex> lock(s.mutex);
  const uint16_t buttons = Be16(gamepad);
  const uint16_t pressed = buttons & ~s.previous[user];
  s.previous[user] = buttons;
  auto idle = [&] { std::memset(gamepad, 0, 12); };

  if (s.mode != Mode::kNone) {  // a LAN screen has the controls
    if (pressed) s.presses.push_back(pressed);
    s.swallow[user] = true;
    return idle();
  }
  if (s.swallow[user]) {  // the press that closed a screen must not reach the game
    if (buttons) return idle();
    s.swallow[user] = false;
  }
  if (OnJoinList() && (pressed & kB)) LeaveJoinList();

  if (!InMultiplayerMenus()) {
    s.held_since[user][0] = s.held_since[user][1] = {};
    return;
  }
  const auto now = Clock::now();
  const uint16_t which[2] = {kY, kBack};
  for (int i = 0; i < 2; ++i) {
    auto& since = s.held_since[user][i];
    if (!(buttons & which[i])) {
      since = {};
      continue;
    }
    if (since == Clock::time_point{}) since = now;
    if (now - since < kHold) continue;
    since = {};
    s.mode = i == 0 ? Mode::kName : Mode::kAddress;
    s.user = user;
    s.presses.clear();
    s.just_opened = true;
    for (bool& w : s.swallow) w = true;
    return idle();
  }
}

namespace {

// Keypads for controllers; a keyboard types straight into the field.
const std::array<const char*, 6> kNameRows = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ .-_",
                                              "abcdefghij", "klmnopqrst", "uvwxyz0123"};
const std::array<const char*, 4> kAddressRows = {"123", "456", "789", ".0"};

class LanOverlay final : public rex::ui::ImGuiDialog {
 public:
  LanOverlay(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path)
      : rex::ui::ImGuiDialog(drawer), config_path_(std::move(config_path)) {}

  bool WantsContinuousRepaint() const override { return open_; }

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!PlayEnabled()) return;
    Mode mode;
    std::deque<uint16_t> presses;
    {
      auto& s = Shared();
      std::lock_guard<std::mutex> lock(s.mutex);
      mode = s.mode;
      presses.swap(s.presses);
      if (s.just_opened) {
        s.just_opened = false;
        Open(mode, s.user);
      }
    }
    open_ = mode != Mode::kNone;
    if (open_) {
      DrawScreen(io, mode, presses);
    } else {
      if (InMultiplayerMenus()) DrawHint(io);
      DrawToast(io);
    }
  }

 private:
  void Open(Mode mode, uint32_t user) {
    row_ = col_ = 0;
    std::memset(text_, 0, sizeof(text_));
    if (mode == Mode::kName) {
      if (auto* p = REX_KERNEL_STATE()->user_profile(user)) {
        const auto name = p->name().substr(0, players::kMaxLength);
        std::memcpy(text_, name.data(), name.size());
      }
    } else {
      std::memcpy(text_, last_address_.data(), std::min(last_address_.size(), sizeof(text_) - 1));
    }
    user_ = user;
  }

  void CloseScreen() {
    auto& s = Shared();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.mode = Mode::kNone;
    s.presses.clear();
  }

  static ImU32 Color(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

  // A button glyph like the game's prompts: a coloured disc with the letter.
  static void Glyph(const char* label, ImU32 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    const ImVec2 size = ImGui::CalcTextSize(label);
    const float w = std::max(h, size.x + h * 0.6f);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), color, h * 0.5f);
    dl->AddText(ImVec2(p.x + (w - size.x) * 0.5f, p.y), Color(255, 255, 255), label);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::SameLine(0, h * 0.3f);
  }

  void BeginPanel(const char* id, ImVec2 pos, ImVec2 pivot, float alpha) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.10f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.75f, 0.10f, 1.0f));
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);
  }
  void EndPanel() {
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
  }

  void DrawHint(ImGuiIO& io) {
    const float scale = std::max(1.0f, io.DisplaySize.y / 720.0f);
    BeginPanel("##ht_lan_hint", ImVec2(io.DisplaySize.x * 0.03f, io.DisplaySize.y * 0.97f),
               ImVec2(0, 1), 0.78f);
    ImGui::SetWindowFontScale(scale);
    ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.15f, 1), "LAN");
    ImGui::SameLine(0, 12 * scale);
    ImGui::TextUnformatted("Hold");
    ImGui::SameLine();
    Glyph("Y", Color(230, 170, 20));
    ImGui::TextUnformatted("Player name");
    ImGui::SameLine(0, 18 * scale);
    ImGui::TextUnformatted("Hold");
    ImGui::SameLine();
    Glyph("BACK", Color(90, 90, 100));
    ImGui::TextUnformatted("Join by IP");
    EndPanel();
  }

  void DrawToast(ImGuiIO& io) {
    if (toast_.empty() || Clock::now() > toast_until_) return;
    const float scale = std::max(1.0f, io.DisplaySize.y / 720.0f);
    BeginPanel("##ht_lan_toast", ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.12f),
               ImVec2(0.5f, 0), 0.9f);
    ImGui::SetWindowFontScale(scale * 1.1f);
    ImGui::TextUnformatted(toast_.c_str());
    EndPanel();
  }

  void Toast(std::string text) {
    toast_ = std::move(text);
    toast_until_ = Clock::now() + std::chrono::seconds(5);
  }

  bool Allowed(Mode mode, char c) const {
    if (mode == Mode::kAddress) return (c >= '0' && c <= '9') || c == '.';
    return players::Allowed(static_cast<unsigned char>(c));
  }
  size_t MaxLength(Mode mode) const { return mode == Mode::kAddress ? 15 : players::kMaxLength; }

  void Type(Mode mode, char c) {
    const size_t n = std::strlen(text_);
    if (n < MaxLength(mode) && Allowed(mode, c)) text_[n] = c, text_[n + 1] = 0;
  }
  void Erase() {
    const size_t n = std::strlen(text_);
    if (n) text_[n - 1] = 0;
  }

  // Returns true when the screen should close.
  bool Confirm(Mode mode) {
    std::string value(text_);
    if (mode == Mode::kName) {
      value = players::Sanitize(value);
      if (value.empty()) return false;
      const uint32_t i = std::min<uint32_t>(user_, players::kCount - 1);
      if (auto* p = REX_KERNEL_STATE()->user_profile(i)) p->set_name(value);
      rex::cvar::SetFlagByName(players::Cvar(int(i)), value);
      try {
        ConfigEdit edit(config_path_);
        edit.Set(players::Cvar(int(i)), "\"" + value + "\"");
        edit.Commit();
      } catch (const std::exception& e) {
        REXLOG_WARN("[ht-lan] could not save the player name: {}", e.what());
      }
      Toast("Player " + std::to_string(i + 1) + " is now \"" + value + "\"");
      return true;
    }
    const auto ip = ParseAddress(value);
    if (!ip || *ip == 0 || *ip == 0xFFFFFFFFu) {
      Toast("\"" + value + "\" is not an IPv4 address (like 192.168.1.20)");
      return false;
    }
    last_address_ = value;
    JoinAddress(*ip);
    if (OnJoinList()) {

      Toast("Added " + value + " - press Y to refresh the list");
    } else {
      Toast("Added " + value + " - now pick Search for Match or Quick Match");
    }
    return true;
  }

  void DrawScreen(ImGuiIO& io, Mode mode, std::deque<uint16_t>& presses) {
    const bool name = mode == Mode::kName;
    const auto* rows = name ? kNameRows.data() : kAddressRows.data();
    const int row_count = name ? int(kNameRows.size()) : int(kAddressRows.size());
    // The keypad's last row holds the two actions.
    const int total_rows = row_count + 1;
    auto row_len = [&](int r) { return r < row_count ? int(std::strlen(rows[r])) : 2; };

    bool close = false, confirm = false;
    for (uint16_t p : presses) {
      if (p & kUp) row_ = (row_ + total_rows - 1) % total_rows;
      if (p & kDown) row_ = (row_ + 1) % total_rows;
      col_ = std::min(col_, row_len(row_) - 1);
      if (p & kLeft) col_ = (col_ + row_len(row_) - 1) % row_len(row_);
      if (p & kRight) col_ = (col_ + 1) % row_len(row_);
      if (p & kX) Erase();
      if (p & kB) close = true;
      if (p & kStart) confirm = true;
      if (p & kA) {
        if (row_ < row_count) Type(mode, rows[row_][col_]);
        else if (col_ == 0) Erase();
        else confirm = true;
      }
    }
    // Keyboard: typed characters, Backspace, Enter, Escape.
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
      const ImWchar c = io.InputQueueCharacters[i];
      if (c < 0x80) Type(mode, char(c));
    }
    io.InputQueueCharacters.resize(0);
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) Erase();
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
      confirm = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) close = true;

    const float scale = std::max(1.0f, io.DisplaySize.y / 720.0f);
    BeginPanel("##ht_lan_screen", ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
               ImVec2(0.5f, 0.5f), 0.96f);
    ImGui::SetWindowFontScale(scale * 1.25f);
    ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.15f, 1), name ? "PLAYER NAME" : "JOIN BY IP");
    ImGui::SetWindowFontScale(scale);
    ImGui::TextDisabled(name ? "Shown to other players in lobbies and the match list."
                             : "Your friend's address on your network or VPN "
                               "(e.g. 192.168.1.20 or a Tailscale 100.x address).\n"
                               "Over a VPN, both players add each other once; the host does it\n"
                               "here before Create Match.");
    ImGui::Spacing();

    // The field shows the text; keyboard and keypad edit the same buffer (an ImGui
    // text box would keep its own copy while focused and hide the keypad's edits).
    {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const ImVec2 size(360 * scale, ImGui::GetTextLineHeight() + 12 * scale);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Color(20, 45, 85), 4 * scale);
      const bool blink = (std::chrono::duration_cast<std::chrono::milliseconds>(
                              Clock::now().time_since_epoch()).count() / 500) % 2 == 0;
      std::string shown = std::string(text_) + (blink ? "|" : " ");
      dl->AddText(ImVec2(p.x + 8 * scale, p.y + 6 * scale), Color(255, 255, 255), shown.c_str());
      ImGui::Dummy(size);
    }
    ImGui::Spacing();

    // Keypad, for controllers.
    const float key = 34 * scale;
    for (int r = 0; r < total_rows; ++r) {
      for (int c = 0; c < row_len(r); ++c) {
        if (c) ImGui::SameLine(0, 4 * scale);
        const bool selected = r == row_ && c == col_;
        ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(0.95f, 0.75f, 0.10f, 1)
                                                        : ImVec4(0.12f, 0.25f, 0.45f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? ImVec4(0, 0, 0, 1) : ImVec4(1, 1, 1, 1));
        char label[16];
        if (r < row_count) {
          if (rows[r][c] == ' ') std::snprintf(label, sizeof(label), "SP##k%d_%d", r, c);
          else std::snprintf(label, sizeof(label), "%c##k%d_%d", rows[r][c], r, c);
        } else {
          std::snprintf(label, sizeof(label), "%s##a%d", c == 0 ? "DELETE" : "OK", c);
        }
        const bool wide = r == row_count;
        if (ImGui::Button(label, ImVec2(wide ? key * 3 : key, key))) {
          row_ = r, col_ = c;
          if (r < row_count) Type(mode, rows[r][c]);
          else if (c == 0) Erase();
          else confirm = true;
        }
        ImGui::PopStyleColor(2);
      }
    }
    ImGui::Spacing();
    Glyph("A", Color(60, 170, 60));
    ImGui::TextUnformatted("Type");
    ImGui::SameLine(0, 14 * scale);
    Glyph("X", Color(40, 110, 210));
    ImGui::TextUnformatted("Delete");
    ImGui::SameLine(0, 14 * scale);
    Glyph("START", Color(90, 90, 100));
    ImGui::TextUnformatted("OK");
    ImGui::SameLine(0, 14 * scale);
    Glyph("B", Color(200, 40, 40));
    ImGui::TextUnformatted("Cancel");
    EndPanel();

    if (confirm && Confirm(mode)) close = true;
    if (close) CloseScreen();
  }

  std::filesystem::path config_path_;
  bool open_ = false;
  char text_[32] = {};
  int row_ = 0, col_ = 0;
  uint32_t user_ = 0;
  std::string last_address_;
  std::string toast_;
  Clock::time_point toast_until_{};
};

}  // namespace

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer,
                                                    std::filesystem::path config_path) {
  return std::make_unique<LanOverlay>(drawer, std::move(config_path));
}

}  // namespace ht::lan
