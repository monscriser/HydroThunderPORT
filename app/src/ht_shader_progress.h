// "Preparando shaders… N de M" — shown while the cached pipelines are created before the game
// starts (rex::ht_progress, tools/wsl/patch_shader_precompile_logs.py). The window used to stay
// black and frozen for these seconds (~7.6 s on the ROG Ally); the SDK now does that work on a
// worker thread so this panel can paint.

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/ht_progress.h>
#include <rex/ui/imgui_dialog.h>

class HtShaderProgress : public rex::ui::ImGuiDialog {
 public:
  explicit HtShaderProgress(rex::ui::ImGuiDrawer* drawer) : rex::ui::ImGuiDialog(drawer) {
    const std::string ui = rex::cvar::GetFlagByName("ht_ui_language");
    lang_ = ui == "en" ? 1 : ui == "es" ? 2 : 0;
  }
  // Keep painting until the start-up pre-compilation is over (nothing else repaints then).
  bool WantsContinuousRepaint() const override { return !rex::ht_progress::GetShaderPreload().finished; }

 protected:
  void OnDraw(ImGuiIO& io) override {
    const auto p = rex::ht_progress::GetShaderPreload();
    if (p.finished || !p.active) return;
    const float scale = std::clamp(std::min(io.DisplaySize.x / 1280.0f, io.DisplaySize.y / 720.0f), 0.75f, 2.0f);
    const ImVec2 size(520.0f * scale, 0.0f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * scale, 18.0f * scale));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.12f, 0.53f, 0.90f, 1.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##ht_shader_progress", nullptr, flags)) {
      ImGui::SetWindowFontScale(scale * 1.15f);
      ImGui::TextUnformatted(T("Preparando shaders", "Preparing shaders", "Preparando shaders"));
      ImGui::SetWindowFontScale(scale);
      char line[160];
      if (p.total == 0) {
        std::snprintf(line, sizeof(line), "%s", T("Lendo o cache de shaders...", "Reading the shader cache...", "Leyendo la caché de shaders..."));
        ImGui::TextDisabled("%s", line);
        // Indeterminate: a short sliding bar.
        const float t = float(std::fmod(ImGui::GetTime(), 1.4) / 1.4);
        ImGui::ProgressBar(t, ImVec2(-1.0f, 10.0f * scale), "");
      } else {
        std::snprintf(line, sizeof(line), T("%u de %u  (compilado uma vez; as corridas ficam sem engasgos)",
                                            "%u of %u  (done once, so races run without stutter)",
                                            "%u de %u  (se hace una vez; las carreras van sin tirones)"),
                      std::min(p.done, p.total), p.total);
        ImGui::TextDisabled("%s", line);
        ImGui::ProgressBar(p.total ? float(std::min(p.done, p.total)) / float(p.total) : 0.0f, ImVec2(-1.0f, 10.0f * scale), "");
      }
      ImGui::TextDisabled("%s", T("Dá para desligar no configurador (Vídeo).", "You can turn this off in the launcher (Video).",
                                  "Se puede desactivar en el configurador (Vídeo)."));
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
  }

 private:
  const char* T(const char* pt, const char* en, const char* es) const { return lang_ == 1 ? en : lang_ == 2 ? es : pt; }
  int lang_ = 0;
};
