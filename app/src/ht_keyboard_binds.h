#pragma once
// Launcher "Controls" tab: keyboard keys for every pad button the game uses.
//
// The keyboard driver (SDK MnkInputDriver) already maps keys to pad buttons through the
// keybind_* cvars; players could not see or change them (jump on X and brake on Q were
// unknown). Each action holds up to two keys, saved as the cvar's comma list ("Q,C").
// Assigning a key that another action uses swaps the two, so one key never presses two
// buttons. Only names the engine parses are offered (rex::ui::ParseVirtualKey), and only
// keys that fire alone: bare Shift/Ctrl/Alt never match a bind, F1-F12 belong to the
// overlays, Escape cancels the capture.

#include <imgui.h>
#include <rex/cvar.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace ht::keys {

struct Action {
  const char* cvar;
  const char* fallback;  // package default (data/hydrothunder.toml)
  const char* pt;
  const char* en;
  const char* es;
};

inline constexpr Action kActions[] = {
    {"keybind_right_trigger", "E", "Acelerar (RT)", "Throttle (RT)", "Acelerar (RT)"},
    {"keybind_left_trigger", "Q", "Frear (LT)", "Brake (LT)", "Frenar (LT)"},
    {"keybind_a", "Space", "Impulso / confirmar (A)", "Boost / confirm (A)", "Impulso / confirmar (A)"},
    {"keybind_x", "X", "Pulo (X)", "Jump (X)", "Salto (X)"},
    {"keybind_lstick_left", "A,Left", "Virar para a esquerda", "Steer left", "Girar a la izquierda"},
    {"keybind_lstick_right", "D,Right", "Virar para a direita", "Steer right", "Girar a la derecha"},
    {"keybind_lstick_up", "W,Up", "Para cima (menus)", "Up (menus)", "Arriba (menus)"},
    {"keybind_lstick_down", "S,Down", "Para baixo (menus)", "Down (menus)", "Abajo (menus)"},
    {"keybind_b", "Backspace", "Voltar (B)", "Back (B)", "Volver (B)"},
    {"keybind_y", "Y", "Botao Y", "Y button", "Boton Y"},
    {"keybind_start", "Return", "Start / pausa", "Start / pause", "Start / pausa"},
    {"keybind_back", "Tab", "Back / Select", "Back / Select", "Back / Select"},
    {"keybind_left_shoulder", "F", "LB", "LB", "LB"},
    {"keybind_right_shoulder", "R", "RB", "RB", "RB"},
};
inline constexpr int kActionCount = int(std::size(kActions));

// ImGui key -> engine key name (same spelling as rex::ui::ParseVirtualKey).
inline constexpr std::pair<ImGuiKey, const char*> kCapturable[] = {
    {ImGuiKey_A, "A"}, {ImGuiKey_B, "B"}, {ImGuiKey_C, "C"}, {ImGuiKey_D, "D"}, {ImGuiKey_E, "E"},
    {ImGuiKey_F, "F"}, {ImGuiKey_G, "G"}, {ImGuiKey_H, "H"}, {ImGuiKey_I, "I"}, {ImGuiKey_J, "J"},
    {ImGuiKey_K, "K"}, {ImGuiKey_L, "L"}, {ImGuiKey_M, "M"}, {ImGuiKey_N, "N"}, {ImGuiKey_O, "O"},
    {ImGuiKey_P, "P"}, {ImGuiKey_Q, "Q"}, {ImGuiKey_R, "R"}, {ImGuiKey_S, "S"}, {ImGuiKey_T, "T"},
    {ImGuiKey_U, "U"}, {ImGuiKey_V, "V"}, {ImGuiKey_W, "W"}, {ImGuiKey_X, "X"}, {ImGuiKey_Y, "Y"},
    {ImGuiKey_Z, "Z"}, {ImGuiKey_0, "0"}, {ImGuiKey_1, "1"}, {ImGuiKey_2, "2"}, {ImGuiKey_3, "3"},
    {ImGuiKey_4, "4"}, {ImGuiKey_5, "5"}, {ImGuiKey_6, "6"}, {ImGuiKey_7, "7"}, {ImGuiKey_8, "8"},
    {ImGuiKey_9, "9"}, {ImGuiKey_Space, "Space"}, {ImGuiKey_Enter, "Return"},
    {ImGuiKey_Backspace, "Backspace"}, {ImGuiKey_Tab, "Tab"}, {ImGuiKey_LeftArrow, "Left"},
    {ImGuiKey_RightArrow, "Right"}, {ImGuiKey_UpArrow, "Up"}, {ImGuiKey_DownArrow, "Down"},
    {ImGuiKey_Insert, "Insert"}, {ImGuiKey_Delete, "Delete"}, {ImGuiKey_Home, "Home"},
    {ImGuiKey_End, "End"}, {ImGuiKey_PageUp, "PageUp"}, {ImGuiKey_PageDown, "PageDown"},
    {ImGuiKey_Minus, "Minus"}, {ImGuiKey_Equal, "Plus"}, {ImGuiKey_LeftBracket, "LBracket"},
    {ImGuiKey_RightBracket, "RBracket"}, {ImGuiKey_GraveAccent, "Backtick"},
};

// Two key slots per action; an empty string is an unused slot.
using Binds = std::array<std::array<std::string, 2>, kActionCount>;

inline Binds Parse(const std::array<std::string, kActionCount>& values) {
  Binds binds{};
  for (int i = 0; i < kActionCount; ++i) {
    std::string v = values[i];
    v.erase(std::remove(v.begin(), v.end(), '"'), v.end());
    int slot = 0;
    size_t start = 0;
    while (start <= v.size() && slot < 2) {
      const size_t comma = v.find(',', start);
      std::string token = v.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      while (!token.empty() && token.front() == ' ') token.erase(0, 1);
      while (!token.empty() && token.back() == ' ') token.pop_back();
      if (!token.empty()) binds[i][slot++] = token;
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
  }
  return binds;
}

inline Binds Load() {
  std::array<std::string, kActionCount> values;
  for (int i = 0; i < kActionCount; ++i) {
    values[i] = rex::cvar::GetFlagByName(kActions[i].cvar);
    if (values[i].empty()) values[i] = kActions[i].fallback;
  }
  return Parse(values);
}

inline Binds Defaults() {
  std::array<std::string, kActionCount> values;
  for (int i = 0; i < kActionCount; ++i) values[i] = kActions[i].fallback;
  return Parse(values);
}

// The cvar text for one action: "\"A,Left\"" (quoted for the toml).
inline std::string Value(const Binds& binds, int action) {
  std::string out;
  for (const auto& key : binds[action]) {
    if (key.empty()) continue;
    if (!out.empty()) out += ',';
    out += key;
  }
  return "\"" + out + "\"";
}

// Puts 'key' in (action, slot). Whoever held 'key' gets this slot's previous key, so
// the two actions swap instead of sharing a key.
inline void Assign(Binds& binds, int action, int slot, const std::string& key) {
  const std::string previous = binds[action][slot];
  for (int i = 0; i < kActionCount; ++i) {
    for (int s = 0; s < 2; ++s) {
      if ((i != action || s != slot) && binds[i][s] == key) binds[i][s] = previous;
    }
  }
  binds[action][slot] = key;
  for (auto& slots : binds) {
    if (slots[0].empty() && !slots[1].empty()) std::swap(slots[0], slots[1]);
    if (slots[0] == slots[1]) slots[1].clear();
  }
}

// Friendly label of an engine key name for the launcher (ASCII, like the rest of it).
inline std::string Label(const std::string& key, int lang) {
  struct Name { const char *key, *pt, *en, *es; };
  static constexpr Name kNames[] = {
      {"Space", "Espaco", "Space", "Espacio"}, {"Return", "Enter", "Enter", "Enter"},
      {"Backspace", "Backspace", "Backspace", "Retroceso"}, {"Left", "Seta esquerda", "Left arrow", "Flecha izquierda"},
      {"Right", "Seta direita", "Right arrow", "Flecha derecha"}, {"Up", "Seta cima", "Up arrow", "Flecha arriba"},
      {"Down", "Seta baixo", "Down arrow", "Flecha abajo"},
  };
  for (const auto& n : kNames) {
    if (key == n.key) return lang == 1 ? n.en : lang == 2 ? n.es : n.pt;
  }
  return key;
}

// Waiting for a key: which action/slot, or -1.
struct Capture {
  int action = -1;
  int slot = 0;
};

// Returns the engine name of a key pressed this frame (nullptr if none), and sets
// 'cancel' on Escape.
inline const char* PressedKey(bool& cancel) {
  cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  for (const auto& [key, name] : kCapturable) {
    if (ImGui::IsKeyPressed(key, false)) return name;
  }
  return nullptr;
}

}  // namespace ht::keys
