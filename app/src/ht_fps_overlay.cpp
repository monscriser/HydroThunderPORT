#include "ht_fps_overlay.h"
#include "ht_lossless.h"

#include <algorithm>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>

namespace {
// Lossless Scaling does not expose its counter to other processes, so the game
// overlay shows what LS will deliver from our measured FPS, the multiplier and
// the display refresh. Checked against the real LS counter in a race:
// 60 FPS x2 on 75 Hz showed "38 / 75" (capped) and "60 / 120" (above refresh).
struct LosslessProbe {
  bool running = false;
  bool active = false;
  double refresh_hz = 0.0;
  std::chrono::steady_clock::time_point next;
};

double DisplayRefreshHz() {
  // DWM's composition rate is the exact refresh (e.g. 74.97), unlike the
  // rounded value from EnumDisplaySettings. Loaded on demand: no link change.
  using Fn = HRESULT(WINAPI*)(HWND, DWM_TIMING_INFO*);
  static const Fn fn = [] {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    return dwm ? reinterpret_cast<Fn>(GetProcAddress(dwm, "DwmGetCompositionTimingInfo")) : nullptr;
  }();
  if (fn) {
    DWM_TIMING_INFO info{};
    info.cbSize = sizeof(info);
    if (SUCCEEDED(fn(nullptr, &info)) && info.rateRefresh.uiDenominator)
      return double(info.rateRefresh.uiNumerator) / info.rateRefresh.uiDenominator;
  }
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  return EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1 ? mode.dmDisplayFrequency : 0.0;
}
}  // namespace
#endif

HtFpsOverlay::HtFpsOverlay(rex::ui::ImGuiDrawer* imgui_drawer)
    : rex::ui::ImGuiDialog(imgui_drawer) {
  frame_ms_.fill(0.0f);
}

void HtFpsOverlay::OnDraw(ImGuiIO& io) {
  (void)io;
  // Measured from our own draws: the overlay is passive, so each draw is one
  // frame delivered by the game (not a host repaint at the monitor rate).
  using Clock = std::chrono::steady_clock;
  const auto now = Clock::now();
  float frame_ms = 0.0f;
  if (last_frame_.time_since_epoch().count() != 0) {
    frame_ms = std::chrono::duration<float, std::milli>(now - last_frame_).count();
  } else {
    window_start_ = now;
  }
  last_frame_ = now;
  ++window_frames_;
  const float window_s = std::chrono::duration<float>(now - window_start_).count();
  if (window_s >= 0.5f) {
    fps_ = float(window_frames_) / window_s;
    window_frames_ = 0;
    window_start_ = now;
  }
  {
    // Host-presented frame rate (what the player sees) for the telemetry log.
    static Clock::time_point log_start = now;
    static uint32_t log_frames = 0;
    static float log_worst = 0.0f;
    ++log_frames;
    log_worst = std::max(log_worst, frame_ms);
    const float log_s = std::chrono::duration<float>(now - log_start).count();
    static const bool log_enabled = rex::cvar::GetFlagByName("ht_perf_log") == "true";
    if (log_s >= 2.0f) {
      if (log_enabled) REXLOG_WARN("[ht-present] presented fps={:.1f} worst={:.1f}ms", log_frames / log_s, log_worst);
      log_start = now;
      log_frames = 0;
      log_worst = 0.0f;
    }
  }
  frame_ms_[history_index_] = frame_ms;
  history_index_ = (history_index_ + 1) % kHistory;

  // Worst frame in the window: an average alone hides exactly the hitches that
  // make a game feel bad.
  const float worst = *std::max_element(frame_ms_.begin(), frame_ms_.end());

  ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.45f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoMove;
  if (ImGui::Begin("##ht_fps", nullptr, flags)) {
    // Average over the same 0.5 s window as the FPS: a per-frame number changes
    // every frame, and frame generation smears such text into ghosts.
    ImGui::Text("%.0f FPS   %.2f ms", fps_, fps_ > 0.0f ? 1000.0f / fps_ : 0.0f);
#if defined(_WIN32)
    // Also detects LS when the player turned it on by hand.
    static LosslessProbe probe;
    if (now >= probe.next) {
      probe.running = ht::lossless::LosslessRunning();
      probe.active = probe.running && ht::lossless::LosslessActive();
      probe.refresh_hz = DisplayRefreshHz();
      probe.next = now + std::chrono::seconds(1);
    }
    const auto& ls = ht::lossless::session_status;
    if (ls.requested || probe.active) {
      const ImVec4 green(0.45f, 0.9f, 0.55f, 1.0f), warn(1.0f, 0.7f, 0.3f, 1.0f);
      if (!probe.running) {
        ImGui::TextColored(warn, "Lossless %dx: Lossless Scaling nao esta aberto", ls.multiplier);
      } else if (!probe.active) {
        ImGui::TextColored(warn, "Lossless %dx: aberto, aguardando ativar", ls.multiplier);
      } else if (!ls.requested) {
        // Started outside the launcher: its multiplier and sync are unknown.
        ImGui::TextColored(green, "Lossless Scaling ATIVO");
      } else {
        double shown = fps_ * ls.multiplier;
        const bool capped = !ls.above_refresh && probe.refresh_hz > 0.0 && shown > probe.refresh_hz + 1.0;
        if (capped) shown = probe.refresh_hz;
        ImGui::TextColored(green, "Lossless %dx ATIVO: ~%.0f FPS na tela", ls.multiplier, shown);
        if (capped)
          ImGui::TextColored(warn, "Limite %.0f Hz: usa ~%.0f dos %.0f FPS do jogo", probe.refresh_hz,
                             shown / ls.multiplier, fps_);
        else if (ls.above_refresh && probe.refresh_hz > 0.0 && shown > probe.refresh_hz + 1.0)
          ImGui::TextDisabled("Acima dos %.0f Hz da tela", probe.refresh_hz);
        ImGui::TextDisabled("Estimado: jogo x %d", ls.multiplier);
      }
    }
#else
    if(ht::lossless::session_status.requested){
      ImGui::TextColored(ImVec4(.4f,.8f,1,1),"Framegen: lsfg-vk %dx solicitado",ht::lossless::session_status.multiplier);
      ImGui::TextDisabled("FPS acima: jogo. Gerados dependem da ordem do overlay Vulkan.");
    }
#endif
    ImGui::PlotLines("##ht_fps_graph", frame_ms_.data(), static_cast<int>(kHistory),
                     static_cast<int>(history_index_), nullptr, 0.0f,
                     std::max(33.4f, worst * 1.1f), ImVec2(180.0f, 32.0f));
    ImGui::TextDisabled("worst %.1f ms", worst);
    // What the presenter really did on its last paint: FSR 3.1 is reported as
    // active only after a successful dispatch, not just because it was chosen.
    using Kind = rex::ui::HtUpscaleStatus::Kind;
    const rex::ui::HtUpscaleStatus up = rex::ui::GetHtUpscaleStatus();
    const ImVec4 green(0.45f, 0.9f, 0.55f, 1.0f), orange(1.0f, 0.7f, 0.3f, 1.0f);
    switch (up.kind) {
      case Kind::kFsr3:
        if (up.fsr3_ok) {
          ImGui::TextColored(green, "FSR 3.1 ativo  %ux%u -> %ux%u", up.input_width,
                             up.input_height, up.output_width, up.output_height);
        } else {
          ImGui::TextColored(orange, "FSR 3.1 indisponivel - usando FSR 1");
        }
        break;
      case Kind::kFsr1:
        ImGui::Text("FSR 1  %ux%u -> %ux%u", up.input_width, up.input_height, up.output_width,
                    up.output_height);
        break;
      case Kind::kBilinear:
        ImGui::TextDisabled("Bilinear  %ux%u -> %ux%u", up.input_width, up.input_height,
                            up.output_width, up.output_height);
        break;
      case Kind::kDownscale:
        ImGui::TextDisabled("Nativa  %ux%u -> %ux%u", up.input_width, up.input_height,
                            up.output_width, up.output_height);
        break;
      case Kind::kNative:
        ImGui::TextDisabled("Sem ampliacao  %ux%u", up.input_width, up.input_height);
        break;
      default:
        break;
    }
  }
  ImGui::End();
}
