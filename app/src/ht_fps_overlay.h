// htrecomp - compact frame rate readout.
//
// Small always-on-top window showing the rate at which frames actually reach
// the screen, plus frame time and a short history graph.
//
// The overlay is passive (WantsContinuousRepaint() == false): it is redrawn
// only when the game delivers a new frame, so the number is the game's real
// frame rate and not the monitor refresh rate. Earlier builds counted every
// host repaint, which followed the display Hz (120/144) even with a 60 cap.

#pragma once

#include <array>
#include <chrono>
#include <cstddef>

#include <rex/ui/imgui_dialog.h>

class HtFpsOverlay : public rex::ui::ImGuiDialog {
 public:
  explicit HtFpsOverlay(rex::ui::ImGuiDrawer* imgui_drawer);

  bool WantsContinuousRepaint() const override { return false; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  static constexpr size_t kHistory = 120;
  std::array<float, kHistory> frame_ms_{};
  size_t history_index_ = 0;
  std::chrono::steady_clock::time_point last_frame_{};
  std::chrono::steady_clock::time_point window_start_{};
  int window_frames_ = 0;
  float fps_ = 0.0f;
};
