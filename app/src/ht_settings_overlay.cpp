#include "ht_settings_overlay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include <imgui.h>
#include <rex/cvar.h>

#include "ht_version.h"

REXCVAR_DEFINE_BOOL(ht_show_fps, false, "Game",
                    "Show the frame rate overlay during play");

namespace {

constexpr float kLabelWidth = 260.0f;
constexpr float kWidgetWidth = 200.0f;

rex::cvar::FlagEntry* Find(const char* name) {
  for (auto& entry : rex::cvar::GetRegistry()) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

// Presets for the `resolution` cvar, which is a free-form string ("720p",
// "1440p", "1280x720", or empty for the window default).
struct ResolutionPreset {
  const char* value;
  const char* label;
};
constexpr ResolutionPreset kResolutionPresets[] = {
    {"", "Auto (window default)"}, {"720p", "1280 x 720"},   {"1080p", "1920 x 1080"},
    {"1440p", "2560 x 1440"},      {"4k", "3840 x 2160"},
};

// The panel, as data.
//
// Every tab is a table of rows, and those tables are the single source of
// truth: rendering, the search filter, "reset to defaults" and the list of
// settings written to the config all walk them. Adding a setting is one line
// here and nothing else.
constexpr HtRow S(const char* cvar, const char* label, const char* help = nullptr,
                  bool optional = false) {
  return HtRow{HtRowKind::kSetting, cvar, label, help, nullptr, 0, optional};
}
constexpr HtRow E(const char* cvar, const char* label, const char* help,
                  const HtEnumOption* options, int count) {
  return HtRow{HtRowKind::kEnum, cvar, label, help, options, count, false};
}
constexpr HtRow Sep() {
  return HtRow{HtRowKind::kSeparator, nullptr, nullptr, nullptr, nullptr, 0, false};
}
constexpr HtRow Note(const char* text) {
  return HtRow{HtRowKind::kNote, nullptr, text, nullptr, nullptr, 0, false};
}
constexpr HtRow Special(HtRowKind kind, const char* cvar) {
  return HtRow{kind, cvar, nullptr, nullptr, nullptr, 0, false};
}

constexpr HtEnumOption kAnisoOptions[] = {
    {-1, "Game default (no override)"},
    {0, "Off"},
    {1, "1x"},
    {2, "2x"},
    {3, "4x"},
    {4, "8x"},
    {5, "16x"},
};

// XLanguage, from the Xbox 360 kernel. The game ships text for some of these;
// picking one it has no strings for falls back to English.
constexpr HtEnumOption kLanguageOptions[] = {
    {1, "English"},
    {2, "Japanese"},
    {3, "German"},
    {4, "French"},
    {5, "Spanish"},
    {6, "Italian"},
    {7, "Korean"},
    {8, "Chinese (Traditional)"},
    {9, "Portuguese"},
    {10, "Chinese (Simplified)"},
    {11, "Polish"},
    {12, "Russian"},
};

constexpr HtEnumOption kQueuePriorityOptions[] = {
    {0, "Normal"},
    {1, "High"},
    {2, "Realtime"},
};

constexpr HtRow kDisplayRows[] = {
    S("resolution", "Resolution",
      "Window and video mode size. The scene still renders at 720p, so this is nearly free."),
    S("fullscreen", "Fullscreen"),
    S("monitor", "Monitor", "0 lets the OS decide. 1 is the primary display."),
    S("window_width", "Window width", "0 uses the resolution above."),
    S("window_height", "Window height", "0 uses the resolution above."),
    Sep(),
    Special(HtRowKind::kFrameRate, nullptr),
    S("d3d12_allow_variable_refresh_rate_and_tearing", "Allow tearing / VRR",
      "Skips the vblank wait. Good on G-Sync or FreeSync, tears on a fixed-refresh display."),
    S("host_present_from_non_ui_thread", "Present off the UI thread",
      "Usually smoother. Turn off if the picture stalls while an overlay is open."),
    Sep(),
    S("present_letterbox", "Letterbox non-native aspect",
      "Keeps the 16:9 shape in a differently shaped window. Off stretches instead."),
    S("present_allow_overscan_cutoff", "Crop to safe area",
      "Crops the edges the way an old CRT did. Off unless you want that look."),
    S("present_safe_area_x", "Safe area width (%)", "Only used when cropping is on."),
    S("present_safe_area_y", "Safe area height (%)", "Only used when cropping is on."),
    S("present_dither", "Output dithering",
      "Hides colour banding in sky and water, at the cost of very fine noise."),
};

constexpr HtRow kGraphicsRows[] = {
    S("resolution_scale", "Internal resolution scale",
      "Renders the scene at this multiple of native. By far the most expensive setting here: "
      "2x costs roughly 8x the GPU time. Raise it one step at a time."),
    E("anisotropic_override", "Anisotropic filtering",
      "Sharpens the track surface ahead of you. Nearly free, and the best value here.",
      kAnisoOptions, int(std::size(kAnisoOptions))),
    S("native_2x_msaa", "Native 2x MSAA",
      "Faster and more accurate. Off only for GPUs that misbehave with it."),
    S("swap_post_effect", "Post anti-aliasing",
      "FXAA on top of the game's own MSAA. Mainly cleans up spray and foliage edges."),
    S("gamma_render_target_as_unorm16", "Accurate gamma render targets",
      "More accurate gamma, for a little extra bandwidth."),
    Sep(),
    S("present_effect", "Upscaling filter",
      "How the 720p image is scaled to the window. fsr = FSR 1 (EASU+RCAS, nearly free). "
      "fsr3 = FSR 3.1 (AMD FidelityFX); without game motion vectors each frame is "
      "reconstructed on its own. Neither generates extra frames."),
    S("present_fsr_quality_mode", "FSR quality mode", nullptr, true),
    S("present_cas_additional_sharpness", "CAS sharpness", nullptr, true),
    S("present_fsr_sharpness_reduction", "FSR sharpness reduction", "Lower is sharper.", true),
    Sep(),
    S("async_shader_compilation", "Async shader compilation",
      "Avoids hitches, but an effect can be missing the first time it appears."),
    S("store_shaders", "Cache compiled shaders",
      "Keeps compiled shaders between runs, so the first lap does not stutter."),
    S("d3d12_pipeline_creation_threads", "Pipeline compile threads",
      "-1 picks a count from your CPU. More threads shorten the first-run stutter."),
    Sep(),
    S("texture_cache_memory_limit_soft", "Texture cache soft limit (MB)",
      "Textures past this are evicted once they have gone unused for the lifetime below."),
    S("texture_cache_memory_limit_soft_lifetime", "Soft limit lifetime (s)"),
    S("texture_cache_memory_limit_hard", "Texture cache hard limit (MB)",
      "Never exceeded. Raise both limits if you scale the internal resolution up."),
    S("texture_cache_memory_limit_render_to_texture", "Render-to-texture limit (MB)"),
};

constexpr HtRow kAudioRows[] = {
    S("audio_mute", "Mute audio"),
    S("audio_maxqframes", "Audio buffer (frames)",
      "Frames queued ahead, 4 to 64. Raise it if you hear crackling, lower it if the sound "
      "lags behind the picture."),
    Sep(),
    Note("The 'XMA: Write to unknown register (0601)' lines in the log are harmless noise."),
};

constexpr HtRow kInputRows[] = {
    S("input_backend", "Input backend",
      "SDL supports more pads and hot-plug. Try XInput if a controller misbehaves."),
    S("guide_button", "Pass through Guide button"),
    Sep(),
    S("mnk_mode", "Mouse & keyboard mode",
      "Keyboard and mouse as a controller. Steering is all-or-nothing, so harsh for driving."),
    S("mnk_sensitivity", "Mouse sensitivity", "Scales mouse movement into right-stick travel."),
    S("mnk_user_index", "Controller slot", "Which of the four pad slots the keyboard occupies."),
    Sep(),
    Note("Key bindings live in the advanced dialog (F4), under Input/Keybinds."),
};

constexpr HtRow kGameRows[] = {
    Special(HtRowKind::kLicense, "license_mask"),
    Special(HtRowKind::kShowFps, "ht_show_fps"),
    E("user_language", "Language",
      "The console language reported to the game. Unsupported ones fall back to English.",
      kLanguageOptions, int(std::size(kLanguageOptions))),
    Sep(),
    Special(HtRowKind::kSaveTools, nullptr),
    Sep(),
    S("xex_apply_patches", "Apply title update if present",
      "Applies a title update file placed beside the game, if you have one."),
    Sep(),
    Special(HtRowKind::kAbout, nullptr),
};

constexpr HtRow kFixRows[] = {
    Note("Compatibility switches. The defaults are already correct for this game."),
    Sep(),
    S("force_additive_depth_test", "Fix lights showing through walls",
      "The game draws point lights with the depth test off, so they show through terrain. "
      "This re-enables it. Turn off if an effect that should sit on top disappears."),
    S("use_fuzzy_alpha_epsilon", "Fuzzy alpha test compare",
      "Fixes flickering cutout geometry on some NVIDIA drivers."),
    S("gpu_allow_invalid_fetch_constants", "Allow invalid fetch constants",
      "Draws through fetch constants the SDK thinks are malformed. On if geometry is missing."),
    S("gpu_3d_to_2d_texture", "3D-to-2D texture wrappers",
      "Samples problematic volume textures through a 2D-compatible path."),
    S("half_pixel_offset", "Half-pixel offset",
      "The console's pixel-centre convention. Off shifts and softens the image."),
    Sep(),
    S("depth_float24_convert_in_pixel_shader", "Convert float24 depth in shader",
      "Exact but slower depth conversion. Try it if distant geometry z-fights."),
    S("depth_float24_round", "Round float24 depth",
      "Rounds rather than truncates that conversion. Also a z-fighting knob."),
    S("occlusion_query_enable", "Host occlusion queries",
      "Off makes everything report visible: slower, but nothing can be wrongly hidden."),
    S("clear_memory_page_state", "Refresh GPU page state each frame",
      "Keeps the CPU and GPU views of memory coherent. Off risks stale data."),
    Sep(),
    E("d3d12_queue_priority", "GPU queue priority",
      "Scheduling priority against other apps. Realtime falls back silently if not granted.",
      kQueuePriorityOptions, int(std::size(kQueuePriorityOptions))),
    S("d3d12_bindless", "Bindless resources",
      "Faster descriptor path where the GPU supports it. Off is the fallback."),
    S("d3d12_tiled_shared_memory", "Tiled shared memory",
      "Maps the guest's 512 MB of RAM sparsely instead of committing all of it."),
};

constexpr HtTab kTabs[] = {
    {"Display", kDisplayRows, int(std::size(kDisplayRows))},
    {"Graphics", kGraphicsRows, int(std::size(kGraphicsRows))},
    {"Audio", kAudioRows, int(std::size(kAudioRows))},
    {"Input", kInputRows, int(std::size(kInputRows))},
    {"Game", kGameRows, int(std::size(kGameRows))},
    {"Fixes", kFixRows, int(std::size(kFixRows))},
};

// The frame rate row drives two cvars and names neither, so they are listed
// here to keep SaveManagedSettings complete.
constexpr const char* kExtraManagedCvars[] = {"vsync", "video_mode_refresh_rate"};

std::string ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return value;
}

bool ContainsNoCase(const char* haystack, const std::string& lowered_needle) {
  return haystack && ToLower(haystack).find(lowered_needle) != std::string::npos;
}

// Formats a value the way TOML wants it: strings quoted, everything else bare.
std::string FormatValue(const rex::cvar::FlagEntry& entry) {
  const std::string value = entry.getter();
  if (entry.type == rex::cvar::FlagType::String) {
    std::string escaped;
    for (char c : value) {
      if (c == '\\' || c == '"') {
        escaped += '\\';
      }
      escaped += c;
    }
    return "\"" + escaped + "\"";
  }
  return value;
}

// True if `line` assigns `key` at the top level (ignoring comments).
bool LineAssigns(const std::string& line, const std::string& key) {
  size_t i = line.find_first_not_of(" 	");
  if (i == std::string::npos || line[i] == '#') {
    return false;
  }
  if (line.compare(i, key.size(), key) != 0) {
    return false;
  }
  size_t j = line.find_first_not_of(" 	", i + key.size());
  return j != std::string::npos && line[j] == '=';
}

void HelpMarker(const char* text) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", text);
  }
}

}  // namespace

namespace {
// Not a member: the input thread has to read it without holding a pointer to
// the dialog, which is created and destroyed on the UI thread.
std::atomic<bool> g_panel_open{false};
}  // namespace

bool HtSettingsPanelOpen() {
  return g_panel_open.load(std::memory_order_relaxed);
}

HtSettingsDialog::HtSettingsDialog(rex::ui::ImGuiDrawer* imgui_drawer,
                                   std::filesystem::path config_path,
                                   std::filesystem::path user_data_root,
                                   std::function<void(bool)> on_fps_toggle)
    : rex::ui::ImGuiDialog(imgui_drawer),
      config_path_(std::move(config_path)),
      user_data_root_(std::move(user_data_root)),
      on_fps_toggle_(std::move(on_fps_toggle)) {
  g_panel_open.store(true, std::memory_order_relaxed);
}

bool HtSettingsDialog::DrawSetting(const char* cvar_name, const char* label, const char* help,
                                   bool optional) {
  auto* entry = Find(cvar_name);
  if (!entry) {
    if (!optional) {
      // Surface a missing cvar rather than hiding the row: it almost certainly
      // means an SDK upgrade renamed or removed something.
      ImGui::TextDisabled("%s (unavailable: %s)", label, cvar_name);
    }
    return false;
  }

  const bool read_only = (entry->lifecycle == rex::cvar::Lifecycle::kInitOnly);
  const bool needs_restart = (entry->lifecycle == rex::cvar::Lifecycle::kRequiresRestart);

  ImGui::PushID(cvar_name);
  if (read_only) {
    ImGui::BeginDisabled();
  }

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (help && help[0]) {
    HelpMarker(help);
  } else if (!entry->description.empty()) {
    HelpMarker(entry->description.c_str());
  }
  ImGui::SameLine(kLabelWidth);
  ImGui::SetNextItemWidth(kWidgetWidth);

  const std::string current = entry->getter();
  bool changed = false;

  if (entry->type == rex::cvar::FlagType::Boolean) {
    bool value = (current == "true");
    if (ImGui::Checkbox("##v", &value)) {
      rex::cvar::SetFlagByName(cvar_name, value ? "true" : "false");
      changed = true;
    }
  } else if (std::strcmp(cvar_name, "resolution") == 0) {
    const char* preview = "Custom";
    for (const auto& preset : kResolutionPresets) {
      if (current == preset.value) {
        preview = preset.label;
        break;
      }
    }
    if (ImGui::BeginCombo("##v", preview)) {
      for (const auto& preset : kResolutionPresets) {
        const bool selected = (current == preset.value);
        if (ImGui::Selectable(preset.label, selected)) {
          rex::cvar::SetFlagByName(cvar_name, preset.value);
          changed = true;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
  } else if (entry->type == rex::cvar::FlagType::String && entry->constraints.HasAllowedValues()) {
    const auto& options = entry->constraints.allowed_values;
    if (ImGui::BeginCombo("##v", current.c_str())) {
      for (const auto& option : options) {
        const bool selected = (option == current);
        if (ImGui::Selectable(option.c_str(), selected)) {
          rex::cvar::SetFlagByName(cvar_name, option);
          changed = true;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
  } else if (entry->type == rex::cvar::FlagType::Int32 ||
             entry->type == rex::cvar::FlagType::Uint32) {
    int value = std::atoi(current.c_str());
    if (entry->constraints.HasRangeConstraint()) {
      const int vmin = entry->constraints.min ? static_cast<int>(*entry->constraints.min) : 0;
      const int vmax = entry->constraints.max ? static_cast<int>(*entry->constraints.max) : 100;
      // A slider is right for a handful of steps and miserable for thousands:
      // hitting exactly 1920 on a 0-8192 drag is not something to ask of
      // anyone. Same reasoning as the refresh rate box.
      const bool wide = (vmax - vmin) > 64;
      const bool edited = wide ? ImGui::InputInt("##v", &value, 1, 64,
                                                 ImGuiInputTextFlags_EnterReturnsTrue)
                               : ImGui::SliderInt("##v", &value, vmin, vmax);
      if (edited) {
        rex::cvar::SetFlagByName(cvar_name, std::to_string(std::clamp(value, vmin, vmax)));
        changed = true;
      }
      if (wide) {
        ImGui::SameLine();
        ImGui::TextDisabled("%d-%d", vmin, vmax);
      }
    } else if (ImGui::InputInt("##v", &value, 1, 16, ImGuiInputTextFlags_EnterReturnsTrue)) {
      rex::cvar::SetFlagByName(cvar_name, std::to_string(value));
      changed = true;
    }
  } else if (entry->type == rex::cvar::FlagType::Double) {
    float value = static_cast<float>(std::atof(current.c_str()));
    if (entry->constraints.HasRangeConstraint()) {
      const float vmin =
          entry->constraints.min ? static_cast<float>(*entry->constraints.min) : 0.0f;
      const float vmax =
          entry->constraints.max ? static_cast<float>(*entry->constraints.max) : 1.0f;
      if (ImGui::SliderFloat("##v", &value, vmin, vmax, "%.2f")) {
        rex::cvar::SetFlagByName(cvar_name, std::to_string(std::clamp(value, vmin, vmax)));
        changed = true;
      }
    } else if (ImGui::InputFloat("##v", &value)) {
      rex::cvar::SetFlagByName(cvar_name, std::to_string(value));
      changed = true;
    }
  } else {
    char buffer[256];
    std::strncpy(buffer, current.c_str(), sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = 0;
    if (ImGui::InputText("##v", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
      rex::cvar::SetFlagByName(cvar_name, buffer);
      changed = true;
    }
  }

  if (needs_restart) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "restart");
  }

  if (read_only) {
    ImGui::EndDisabled();
  }
  ImGui::PopID();

  if (changed && needs_restart) {
    restart_pending_ = true;
  }
  return changed;
}

bool HtSettingsDialog::DrawEnumSetting(const char* cvar_name, const char* label,
                                       const char* help, const HtEnumOption* options,
                                       int count) {
  auto* entry = Find(cvar_name);
  if (!entry) {
    ImGui::TextDisabled("%s (unavailable: %s)", label, cvar_name);
    return false;
  }
  const bool needs_restart = (entry->lifecycle == rex::cvar::Lifecycle::kRequiresRestart);
  ImGui::PushID(cvar_name);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  if (help && help[0]) {
    HelpMarker(help);
  }
  ImGui::SameLine(kLabelWidth);
  ImGui::SetNextItemWidth(kWidgetWidth);

  const int current = std::atoi(entry->getter().c_str());
  const char* preview = "Custom";
  for (int i = 0; i < count; ++i) {
    if (options[i].value == current) {
      preview = options[i].label;
    }
  }
  bool changed = false;
  if (ImGui::BeginCombo("##v", preview)) {
    for (int i = 0; i < count; ++i) {
      const bool selected = (options[i].value == current);
      if (ImGui::Selectable(options[i].label, selected)) {
        rex::cvar::SetFlagByName(cvar_name, std::to_string(options[i].value));
        changed = true;
        if (needs_restart) {
          restart_pending_ = true;
        }
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
  if (needs_restart) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "restart");
  }
  ImGui::PopID();
  return changed;
}

bool HtSettingsDialog::DrawLicenseToggle() {
  auto* entry = Find("license_mask");
  if (!entry) {
    return false;
  }
  ImGui::PushID("license_mask");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Full game and DLC license");
  HelpMarker("This build reports the full XBLA license mask to the game and installed DLC. "
             "Install the game, DLC and update using the setup window.");
  ImGui::SameLine(kLabelWidth);
  ImGui::SetNextItemWidth(kWidgetWidth);

  ImGui::TextUnformatted("Full game + installed DLC");
  ImGui::PopID();
  return false;
}

void HtSettingsDialog::DrawFrameRateControls() {
  // The swap limiter is independent of the virtual console's vblank clock.
  // Keep that clock enabled when removing the rendering cap.
  auto* vsync = Find("vsync");
  if (vsync) {
    const bool capped = std::atoi(rex::cvar::GetFlagByName("fps_limit").c_str()) > 0;
    ImGui::PushID("ht_framerate_mode");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Frame rate");
    HelpMarker(
        "Unlimited removes the swap limiter while preserving the virtual console clock at 60 Hz. "
        "The compositor, driver or frame generator can still limit visible presentation. "
        "Very high frame rates require physics and audio testing.");
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(kWidgetWidth);
    const char* modes[] = {"Unlimited (experimental)", "Limited"};
    int mode = capped ? 1 : 0;
    if (ImGui::Combo("##v", &mode, modes, 2)) {
      rex::cvar::SetFlagByName("vsync", "true");
      rex::cvar::SetFlagByName("fps_limit", mode == 1 ? "60" : "0");
      rex::cvar::SetFlagByName("video_mode_refresh_rate", "60");
      restart_pending_ = true;
    }
    ImGui::PopID();
  }

  // Typed, not a slider: hitting exactly 144 by dragging is miserable.
  auto* rate = Find("video_mode_refresh_rate");
  if (rate) {
    const bool uncapped = std::atoi(rex::cvar::GetFlagByName("fps_limit").c_str()) <= 0;
    ImGui::PushID("video_mode_refresh_rate");
    if (uncapped) {
      ImGui::BeginDisabled();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Refresh rate (Hz)");
    HelpMarker(
        "The guest video mode the game paces against. Read once when the vblank thread starts, so "
        "it only takes effect after a restart. Ignored entirely while uncapped.");
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(kWidgetWidth);

    const double vmin = rate->constraints.min ? *rate->constraints.min : 24.0;
    const double vmax = rate->constraints.max ? *rate->constraints.max : 240.0;
    double value = std::atof(rate->getter().c_str());
    if (ImGui::InputDouble("##v", &value, 1.0, 10.0, "%.2f",
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
      value = std::clamp(value, vmin, vmax);
      rex::cvar::SetFlagByName("video_mode_refresh_rate", std::to_string(value));
      rex::cvar::SetFlagByName("fps_limit", std::to_string(int(value)));
      restart_pending_ = true;
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "restart");

    ImGui::Dummy(ImVec2(kLabelWidth - 8.0f, 0.0f));
    ImGui::SameLine();
    for (double preset : {60.0, 75.0, 120.0, 144.0, 165.0, 240.0}) {
      char label[16];
      std::snprintf(label, sizeof(label), "%g", preset);
      if (preset > vmax) {
        continue;
      }
      if (ImGui::SmallButton(label)) {
        rex::cvar::SetFlagByName("video_mode_refresh_rate", std::to_string(preset));
        rex::cvar::SetFlagByName("fps_limit", std::to_string(int(preset)));
        restart_pending_ = true;
      }
      ImGui::SameLine();
    }
    ImGui::NewLine();

    if (uncapped) {
      ImGui::EndDisabled();
    }
    ImGui::PopID();
  }
}

HtSettingsDialog::~HtSettingsDialog() {
  g_panel_open.store(false, std::memory_order_relaxed);
  // Persist on close as well as on the button. Losing a change because the
  // panel was dismissed without noticing a save button is exactly the failure
  // this panel exists to avoid, and the merge-based write is safe to repeat.
  SaveManagedSettings();
}

void HtSettingsDialog::SaveManagedSettings() {
  // Read the existing config so anything we do not manage survives verbatim.
  std::vector<std::string> lines;
  {
    std::ifstream in(config_path_);
    if (in) {
      std::string line;
      while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        lines.push_back(std::move(line));
      }
    }
  }

  // A bare key written after a [table] header would belong to that table, so
  // new keys have to go in before the first one.
  size_t insert_at = lines.size();
  for (size_t i = 0; i < lines.size(); ++i) {
    size_t j = lines[i].find_first_not_of(" 	");
    if (j != std::string::npos && lines[i][j] == '[') {
      insert_at = i;
      break;
    }
  }

  // Every cvar named by any row, plus the ones the composite rows drive.
  std::vector<const char*> managed;
  for (const auto& tab : kTabs) {
    for (int i = 0; i < tab.row_count; ++i) {
      if (tab.rows[i].cvar) {
        managed.push_back(tab.rows[i].cvar);
      }
    }
  }
  for (const char* name : kExtraManagedCvars) {
    managed.push_back(name);
  }

  std::vector<std::string> appended;
  for (const char* name : managed) {
    const auto* entry = Find(name);
    if (!entry) {
      continue;
    }
    const std::string assignment = std::string(name) + " = " + FormatValue(*entry);
    bool replaced = false;
    for (auto& line : lines) {
      if (LineAssigns(line, name)) {
        line = assignment;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      appended.push_back(assignment);
    }
  }

  static constexpr const char* kMarker = "# Written by the in-game settings panel (F5).";
  if (!appended.empty()) {
    // Append after the marker a previous save already wrote, rather than
    // emitting a fresh header every time the managed set grows.
    for (size_t i = 0; i < lines.size(); ++i) {
      if (lines[i] == kMarker) {
        insert_at = i + 1;
        break;
      }
    }
    if (insert_at == lines.size()) {
      lines.push_back("");
      lines.push_back(kMarker);
      insert_at = lines.size();
    }
    lines.insert(lines.begin() + static_cast<ptrdiff_t>(insert_at), appended.begin(),
                 appended.end());
  }

  std::ofstream out(config_path_, std::ios::trunc);
  if (!out) {
    return;
  }
  for (const auto& line : lines) {
    out << line << "\n";
  }
  saved_ = true;
  restart_pending_ = false;
}

void HtSettingsDialog::DrawShowFpsToggle() {
  auto* fps = Find("ht_show_fps");
  if (!fps) {
    return;
  }
  ImGui::PushID("ht_show_fps");
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Show frame rate");
  HelpMarker(
      "Small overlay with the current frame rate, frame time and a short history. It measures "
      "frames actually presented, which is what you see - not the guest's internal simulation "
      "rate. Keeping it open forces the presenter onto the UI thread, so it is not completely "
      "free. Also on F6.");
  ImGui::SameLine(kLabelWidth);
  ImGui::SetNextItemWidth(kWidgetWidth);
  bool on = (fps->getter() == "true");
  if (ImGui::Checkbox("##v", &on)) {
    rex::cvar::SetFlagByName("ht_show_fps", on ? "true" : "false");
    if (on_fps_toggle_) {
      on_fps_toggle_(on);
    }
  }
  ImGui::PopID();
}

namespace {

// The console lays a save out as
//   user_data/<console id>/<title id>/00000001/<name>/<name>
// and this title is 5841096A. Searching rather than hardcoding the console id,
// which differs per install.
std::filesystem::path FindSavePath(const std::filesystem::path& user_data_root) {
  std::error_code ec;
  if (user_data_root.empty() || !std::filesystem::is_directory(user_data_root, ec)) {
    return {};
  }
  for (const auto& console : std::filesystem::directory_iterator(user_data_root, ec)) {
    if (!console.is_directory()) {
      continue;
    }
    const auto candidate =
        console.path() / "5841096A" / "00000001" / "ht2save.dat" / "ht2save.dat";
    if (std::filesystem::is_regular_file(candidate, ec)) {
      return candidate;
    }
  }
  return {};
}

std::string TimeStamp() {
  const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &tm);
  return buffer;
}

}  // namespace

// Build identity and the keys a player needs, in the panel itself.
//
// A shipped build has to be able to answer "which version is this?" without
// reading a log, and the keybinds are otherwise only discoverable by knowing
// they exist. F7 and F8 are deliberately absent here - they are on the
// Developer tab, where they belong.
void HtSettingsDialog::DrawAbout() {
  ImGui::TextDisabled("%s %s", HT_BUILD_TITLE, HT_BUILD_STAMP);
  ImGui::TextDisabled("Hydro Thunder Hurricane, statically recompiled with rexglue.");
  ImGui::Spacing();
  ImGui::TextDisabled("F5 settings    F6 frame rate    F4 all cvars    F3 debug overlay");
}

void HtSettingsDialog::DrawSaveTools() {
  const auto save = FindSavePath(user_data_root_);

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Save file");
  HelpMarker(
      "This game checksums its save and REJECTS one that does not match, wiping progress back "
      "to a fresh profile. A backup is the only recovery, and it has to live outside the save "
      "folder because the game recreates that folder wholesale. Backups go next to the "
      "executable, in save_backups/.");
  ImGui::SameLine(kLabelWidth);

  if (save.empty()) {
    ImGui::TextDisabled("not found yet");
    return;
  }

  std::error_code ec;
  const auto size = std::filesystem::file_size(save, ec);
  ImGui::Text("%llu KB", static_cast<unsigned long long>(size / 1024));

  ImGui::Dummy(ImVec2(kLabelWidth - 8.0f, 0.0f));
  ImGui::SameLine();
  if (ImGui::Button("Back up now")) {
    const auto dir = config_path_.parent_path() / "save_backups";
    std::filesystem::create_directories(dir, ec);
    const auto target = dir / ("ht2save-" + TimeStamp() + ".dat");
    std::filesystem::copy_file(save, target, std::filesystem::copy_options::overwrite_existing,
                               ec);
    save_status_ = ec ? ("failed: " + ec.message()) : ("saved to " + target.string());
  }
  ImGui::SameLine();
  if (ImGui::Button("Open backup folder")) {
    const auto dir = config_path_.parent_path() / "save_backups";
    std::filesystem::create_directories(dir, ec);
#if defined(_WIN32)
    ::ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#endif
  }
  if (!save_status_.empty()) {
    ImGui::Dummy(ImVec2(kLabelWidth - 8.0f, 0.0f));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", save_status_.c_str());
  }
}

void HtSettingsDialog::DrawRows(const HtRow* rows, int count, const std::string& filter) {
  const bool filtering = !filter.empty();
  for (int i = 0; i < count; ++i) {
    const HtRow& row = rows[i];
    if (filtering) {
      // Structural rows have nothing to match, and a filtered list of
      // separators reads as noise.
      if (row.kind == HtRowKind::kSeparator || row.kind == HtRowKind::kNote) {
        continue;
      }
      if (!ContainsNoCase(row.label, filter) && !ContainsNoCase(row.cvar, filter)) {
        continue;
      }
    }
    switch (row.kind) {
      case HtRowKind::kSetting:
        DrawSetting(row.cvar, row.label, row.help, row.optional);
        break;
      case HtRowKind::kEnum:
        DrawEnumSetting(row.cvar, row.label, row.help, row.options, row.option_count);
        break;
      case HtRowKind::kSeparator:
        ImGui::Separator();
        break;
      case HtRowKind::kNote:
        ImGui::TextWrapped("%s", row.label);
        break;
      case HtRowKind::kLicense:
        DrawLicenseToggle();
        break;
      case HtRowKind::kShowFps:
        DrawShowFpsToggle();
        break;
      case HtRowKind::kFrameRate:
        DrawFrameRateControls();
        break;
      case HtRowKind::kSaveTools:
        DrawSaveTools();
        break;
      case HtRowKind::kAbout:
        DrawAbout();
        break;
    }
  }
}

void HtSettingsDialog::ResetRows(const HtRow* rows, int count) {
  for (int i = 0; i < count; ++i) {
    const HtRow& row = rows[i];
    if (!row.cvar) {
      continue;
    }
    // license_mask is not a preference to be tuned - it states whether this is
    // a copy you own. Its default is 0, which presents a full retail dump as
    // the trial: no maps, modes or boats. Resetting it would silently re-lock
    // the game, and the player would have no idea which button did it.
    if (row.kind == HtRowKind::kLicense) {
      continue;
    }
    const auto* entry = Find(row.cvar);
    if (!entry || entry->lifecycle == rex::cvar::Lifecycle::kInitOnly) {
      continue;
    }
    if (entry->getter() == entry->default_value) {
      continue;
    }
    rex::cvar::SetFlagByName(row.cvar, entry->default_value);
    if (entry->lifecycle == rex::cvar::Lifecycle::kRequiresRestart) {
      restart_pending_ = true;
    }
  }
  // The composite rows own cvars they do not name.
  if (rows == kDisplayRows) {
    for (const char* name : kExtraManagedCvars) {
      const auto* entry = Find(name);
      if (entry && entry->getter() != entry->default_value) {
        rex::cvar::SetFlagByName(name, entry->default_value);
        restart_pending_ = true;
      }
    }
  }
  // ht_show_fps has a side effect beyond its value.
  if (rows == kGameRows && on_fps_toggle_) {
    on_fps_toggle_(rex::cvar::GetFlagByName("ht_show_fps") == "true");
  }
}

void HtSettingsDialog::OnDraw(ImGuiIO& /*io*/) {
  ImGui::SetNextWindowSize(ImVec2(700.0f, 560.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Hydro Thunder Settings##ht", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  ImGui::SetNextItemWidth(220.0f);
  ImGui::InputTextWithHint("##ht_filter", "Search settings", filter_, sizeof(filter_));
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    filter_[0] = 0;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("|  PgUp/PgDn tabs  |  F4 every cvar  |  F6 frame rate");
  ImGui::Separator();

  // Page Up / Page Down move between tabs. ImGui's own tab bar is mouse-only,
  // and this panel has to be usable without one.
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
    const int tab_count = int(std::size(kTabs));
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true)) {
      active_tab_ = (active_tab_ + 1) % tab_count;
      force_active_tab_ = true;
    } else if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true)) {
      active_tab_ = (active_tab_ + tab_count - 1) % tab_count;
      force_active_tab_ = true;
    }
  }

  const std::string filter = ToLower(filter_);

  ImGui::BeginChild("##ht_body", ImVec2(0.0f, -64.0f), false);
  if (!filter.empty()) {
    // Searching crosses tabs: a player looking for "sharpness" should not have
    // to know whether it lives under Display or Graphics.
    int hits = 0;
    for (const auto& tab : kTabs) {
      int tab_hits = 0;
      for (int i = 0; i < tab.row_count; ++i) {
        const HtRow& row = tab.rows[i];
        if (row.kind == HtRowKind::kSeparator || row.kind == HtRowKind::kNote) {
          continue;
        }
        if (ContainsNoCase(row.label, filter) || ContainsNoCase(row.cvar, filter)) {
          ++tab_hits;
        }
      }
      if (tab_hits == 0) {
        continue;
      }
      hits += tab_hits;
      ImGui::SeparatorText(tab.name);
      DrawRows(tab.rows, tab.row_count, filter);
    }
    if (hits == 0) {
      ImGui::Spacing();
      ImGui::TextDisabled("Nothing here matches \"%s\".", filter_);
      ImGui::TextWrapped(
          "This panel is a curated subset. Press F4 for the SDK dialog, which lists all %d "
          "registered cvars by raw name.",
          int(rex::cvar::GetRegistry().size()));
    }
  } else if (ImGui::BeginTabBar("##ht_tabbar")) {
    for (int index = 0; index < int(std::size(kTabs)); ++index) {
      const HtTab& tab = kTabs[index];
      const ImGuiTabItemFlags flags = (force_active_tab_ && index == active_tab_)
                                          ? ImGuiTabItemFlags_SetSelected
                                          : ImGuiTabItemFlags_None;
      if (!ImGui::BeginTabItem(tab.name, nullptr, flags)) {
        continue;
      }
      if (!force_active_tab_) {
        if (active_tab_ != index) {
          reset_armed_tab_ = -1;
        }
        active_tab_ = index;
      }
      ImGui::Spacing();
      DrawRows(tab.rows, tab.row_count, filter);
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::PushID(tab.name);
      const bool armed = (reset_armed_tab_ == index);
      if (ImGui::SmallButton(armed ? "Click again to confirm reset"
                                   : "Reset this tab to defaults")) {
        if (armed) {
          ResetRows(tab.rows, tab.row_count);
          reset_armed_tab_ = -1;
        } else {
          reset_armed_tab_ = index;
        }
      }
      if (armed) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) {
          reset_armed_tab_ = -1;
        }
      }
      ImGui::PopID();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    force_active_tab_ = false;
  }
  ImGui::EndChild();

  ImGui::Separator();
  if (ImGui::Button("Save to config")) {
    SaveManagedSettings();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("(%s)", config_path_.filename().string().c_str());

  if (restart_pending_) {
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f),
                       "Settings marked \"restart\" take effect after restarting.");
  } else if (saved_) {
    ImGui::TextDisabled("Saved. Settings are also written automatically when this closes.");
  } else {
    ImGui::TextDisabled("Settings are written automatically when this panel closes.");
  }

  ImGui::End();
}
