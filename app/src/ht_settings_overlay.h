// htrecomp - player-facing settings overlay.
//
// A curated companion to the SDK's own settings dialog (F4), which lists every
// registered cvar by raw name. This one shows only the handful a player cares
// about, with readable labels, real sliders, and presets.
//
// Deliberately lives in htrecomp rather than in third_party/rexglue-sdk: it
// needs no SDK changes at all, so it cannot be lost to an SDK upgrade. The
// attach point is RexApp::OnCreateDialogs plus rex::ui::RegisterBind, both of
// which the SDK provides for exactly this. See SDK_PATCHES.md for why keeping
// game-specific code out of the vendored tree matters.

#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include <rex/ui/imgui_dialog.h>

namespace rex::cvar {
struct FlagEntry;
}

// One choice in a dropdown backed by an integer cvar. Raw numbers are
// meaningless to a player: "anisotropic_override = 3" is really "4x".
struct HtEnumOption {
  int value;
  const char* label;
};

// How one row of the panel is rendered. Most rows are plain cvars rendered
// from their registered type; the rest need a hand-written widget.
enum class HtRowKind {
  kSetting,    // cvar, rendered by type (checkbox / slider / combo / text)
  kEnum,       // integer cvar, rendered as a named dropdown
  kSeparator,  // horizontal rule
  kNote,       // wrapped explanatory text, no cvar
  kLicense,    // license_mask, as a single "unlock full game" checkbox
  kShowFps,    // ht_show_fps, which also has to create/destroy the overlay
  kFrameRate,  // vsync + video_mode_refresh_rate, as one coherent control
  kSaveTools,  // save file status and the "back up now" button
  kAbout,      // build identity and the player-facing keybinds
};

// A single row. The panel is described entirely by tables of these, so the
// rendering, the search filter, the "reset to defaults" button and the list of
// settings that get written to the config all read the same source and cannot
// drift apart.
struct HtRow {
  HtRowKind kind = HtRowKind::kSetting;
  const char* cvar = nullptr;
  const char* label = nullptr;
  const char* help = nullptr;
  const HtEnumOption* options = nullptr;
  int option_count = 0;
  // True for a setting that legitimately may not exist in this build (a
  // compile-time feature such as FidelityFX). Those are skipped silently;
  // everything else renders an "unavailable" row, so an SDK rename shows up
  // instead of quietly disappearing from the UI.
  bool optional = false;
};

struct HtTab {
  const char* name;
  const HtRow* rows;
  int row_count;
};

// True while the F5 panel is open. Read from the input polling thread to
// suspend mouse-and-keyboard controller emulation, which otherwise re-centres
// and hides the cursor every frame and makes the panel unclickable. See the
// comment on the callback in main.cpp.
bool HtSettingsPanelOpen();

class HtSettingsDialog : public rex::ui::ImGuiDialog {
 public:
  // `on_fps_toggle` creates or destroys the frame rate overlay; the dialog
  // cannot own it, because the overlay must outlive this panel being closed.
  HtSettingsDialog(rex::ui::ImGuiDrawer* imgui_drawer, std::filesystem::path config_path,
                   std::filesystem::path user_data_root,
                   std::function<void(bool)> on_fps_toggle);
  ~HtSettingsDialog();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  // Renders one labelled row; returns true if the value changed this frame.
  // `optional` marks a setting that legitimately may not exist in this build
  // (a compile-time feature such as FidelityFX). Those are skipped silently;
  // everything else renders an "unavailable" row so an SDK rename is visible
  // rather than quietly disappearing from the UI.
  bool DrawSetting(const char* cvar_name, const char* label, const char* help,
                   bool optional = false);
  // license_mask is a bitfield, but bit 0 is the only one this game reads.
  bool DrawLicenseToggle();
  bool DrawEnumSetting(const char* cvar_name, const char* label, const char* help,
                       const HtEnumOption* options, int count);
  void DrawShowFpsToggle();
  // Save file status and a one-click backup. This game rejects a save whose
  // checksum does not match and resets progress when it does - a backup that
  // the game itself cannot reach is the only real protection, and the reason
  // this is in the player-facing panel rather than a script.
  void DrawSaveTools();
  void DrawAbout();

  // Renders a table of rows. When `filter` is non-empty only rows whose label
  // or cvar name contains it are drawn, and the structural rows (separators,
  // notes) are dropped so the result reads as a flat list of hits.
  void DrawRows(const HtRow* rows, int count, const std::string& filter);
  // Restores every cvar in a table to its registered default.
  void ResetRows(const HtRow* rows, int count);

  // Writes the settings this panel manages into config_path_, updating keys in
  // place and leaving every other line - comments included - untouched.
  //
  // Deliberately NOT rex::cvar::SaveConfig: that serialises the whole registry
  // to a fresh file, so it discards the config's comments, bakes absolute
  // machine paths (game_data_root, cache_path) into it, and drops any setting
  // whose value happens to equal its default. The last part is the dangerous
  // one - a license_mask that reads back as 0 simply vanishes, silently
  // re-locking the game on next launch.
  void SaveManagedSettings();

  // Frame rate mode and refresh rate need hand-written widgets: the generic
  // slider is too coarse to hit 144 exactly, and "uncapped" is the `vsync`
  // cvar inverted rather than a setting of its own.
  void DrawFrameRateControls();

  std::filesystem::path config_path_;
  std::filesystem::path user_data_root_;
  std::string save_status_;
  // Search box contents. Non-empty replaces the tabs with a flat result list;
  // 190-odd cvars are hard to browse but easy to search.
  char filter_[64] = {};
  // Selected tab, tracked rather than left to ImGui so Page Up / Page Down can
  // move between tabs. The panel is reachable from a controller-style setup
  // where a mouse may not be to hand.
  int active_tab_ = 0;
  bool force_active_tab_ = false;
  // Index of the tab whose reset button has been pressed once. Resetting a
  // whole tab is not something to do by accident on a misclick.
  int reset_armed_tab_ = -1;
  std::function<void(bool)> on_fps_toggle_;
  bool restart_pending_ = false;
  bool saved_ = false;
};
