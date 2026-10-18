#pragma once

#include <rex/rex_app.h>
#include <rex/filesystem.h>
#include <rex/system.h>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/ui/window.h>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

// Defined in ht_version.h so the settings panel can show the build without
// pulling in the whole app framework. One definition, two consumers.
#include "ht_setup.h"
#include "ht_setup_wizard.h"
#include "ht_content_install.h"
#include "ht_version.h"
#include "ht_library_paths.h"
#include "ht_lossless.h"

// Debug console, off unless the build asks for it. See where it is used.
#ifndef HT_DEV_CONSOLE
#define HT_DEV_CONSOLE 0
#endif

class HtBaseApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  virtual std::string_view GetBuildTitle() const = 0;
  virtual std::string_view GetBuildStamp() const = 0;
  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto executable=rex::filesystem::GetExecutableFolder();
    library_root_=ht::LibraryRoot(executable);
    const char* testing=std::getenv("HT_BOOT_TEST");
    if(!(testing && std::strcmp(testing,"1")==0)) {
      const auto settings=ht::SettingsDirectory();
      std::filesystem::create_directories(settings);
      std::ofstream(settings/"build-version.txt")<<GetBuildStamp()<<"\n";
      paths.config_path=settings/"hydrothunder.toml";
      if(!std::filesystem::exists(paths.config_path) && std::filesystem::exists(executable/"hydrothunder.toml"))
        std::filesystem::copy_file(executable/"hydrothunder.toml",paths.config_path);
      paths.cache_root=ht::ShaderCacheDirectory(library_root_);
      paths.metadata_root=settings;
      if(!ht::ReadyForLaunch(library_root_) && library_root_==executable)
        library_root_=settings/"library";
    }
    paths.game_data_root=ht::GameDirectory(library_root_);
    paths.user_data_root=library_root_/"user_data";
  }

  bool SetupEnvironment() override {
    FILE* f = nullptr;
#if HT_STARTUP_TRACE
    f = fopen("startup_debug.txt", "w");
#endif
    if (f) { fprintf(f, "Entered SetupEnvironment\n"); fflush(f); }

    // 1. Determine local paths
    auto exe_dir = rex::filesystem::GetExecutableFolder();
    if (f) { fprintf(f, "Got exe_dir: %s\n", exe_dir.string().c_str()); fflush(f); }

    auto game_data = (exe_dir / "game_data").string();
    auto user_data = (exe_dir / "user_data").string();
#if defined(_WIN32)
    auto cache_data = (exe_dir / "cache_windows").string();
#else
    auto cache_data = (exe_dir / "cache_linux").string();
#endif
    if (f) { fprintf(f, "Constructed path strings\n"); fflush(f); }

    // 2. Override CVARs to force portable mode and bypass the GetUserFolder() hang
    REXCVAR_SET(game_data_root, game_data);
    REXCVAR_SET(user_data_root, user_data);
    REXCVAR_SET(cache_root, cache_data);
    if (f) { fprintf(f, "Set CVARs\n"); fflush(f); }

    // 3. Call base implementation
    if (f) { fprintf(f, "Calling rex::ReXApp::SetupEnvironment()\n"); fflush(f); }
    bool res = rex::ReXApp::SetupEnvironment();
    const bool playing=ht::ReadyForLaunch(library_root_) &&
      (rex::cvar::GetFlagByName("ht_direct_launch")=="true" ||
       rex::cvar::GetFlagByName("ht_import_on_start")=="false");
    bool lossless_requested=false;
    try { lossless_requested=ht::lossless::Prepare(playing); }
    catch(const std::exception& error) {
      spdlog::error("[ht-lsfg] {}",error.what());
      rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error,error.what());
      if(f)fclose(f);
      return false;
    }
    if(lossless_requested) {
      spdlog::info("[ht-lsfg] Frame generation requested; actual generated FPS must be checked with Lossless Scaling/lsfg-vk.");
      if(ht::lossless::Load().above_refresh)
        rex::cvar::SetFlagByName("ht_present_tearing","false");
#if defined(_WIN32)
      // The Windows capture path needs windowed/borderless presentation.
      rex::cvar::SetFlagByName("fullscreen_mode","borderless");
#endif
    }
    if (f) { fprintf(f, "Finished rex::ReXApp::SetupEnvironment(): %d\n", res); fflush(f); }

    rex::cvar::SetFlagByName("license_mask", "4294967295");
    // Same as the Xenia Canary profile this game is known to run well with on
    // the ROG Ally: memexport readback forces a GPU->CPU sync in the middle of
    // the frame and page-state refresh costs CPU every frame; Hydro Thunder
    // needs neither. ht_compat_* lets a player turn them back on.
    if (rex::cvar::GetFlagByName("ht_compat_memexport_readback") != "true")
      rex::cvar::SetFlagByName("readback_memexport", "false");
    if (rex::cvar::GetFlagByName("ht_compat_page_state") != "true")
      rex::cvar::SetFlagByName("clear_memory_page_state", "false");
    rex::cvar::SetFlagByName("primitive_processor_cache_min_indices", "4096");
    // Remaining Xenia Canary parity (the profile used on the ROG Ally):
    // 360-accurate non-seamless cube maps (also cheaper), CPU vertex shading
    // for unclipped draws, and dithered output for smoother gradients.
    rex::cvar::SetFlagByName("non_seamless_cube_map", "true");
    rex::cvar::SetFlagByName("execute_unclipped_draw_vs_on_cpu", "true");
    rex::cvar::SetFlagByName("present_dither", "true");
    rex::cvar::SetFlagByName("local_network_only", "true");
    // Avoid a busy presentation loop repeatedly displaying an unchanged guest
    // frame. The simulation's vblank remains independent of the host display.
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_immediate",
                             rex::cvar::GetFlagByName("ht_present_tearing") == "true" ? "true" : "false");
    // Mailbox is safe now that the host only repaints on new guest frames
    // (no busy loop); it lowers latency without tearing. FIFO is the fallback.
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_mailbox", "true");
    rex::cvar::SetFlagByName("vulkan_allow_present_mode_fifo_relaxed", "false");

    // Setup uses a regular desktop window; fullscreen is chosen when playing.
    const char* boot_test = std::getenv("HT_BOOT_TEST");
    const bool test_mode = boot_test && std::strcmp(boot_test, "1") == 0;
    // GPU A/B tests only (tools/test/gpu_race.ps1): "name=value;name=value"
    // applied over the forced values above, which the toml cannot change.
    if (const char* overrides = std::getenv("HT_TEST_CVARS"); test_mode && overrides) {
      std::string list = overrides;
      for (size_t start = 0; start < list.size();) {
        size_t end = list.find(';', start);
        if (end == std::string::npos) end = list.size();
        const std::string item = list.substr(start, end - start);
        if (const size_t eq = item.find('='); eq != std::string::npos) {
          rex::cvar::SetFlagByName(item.substr(0, eq), item.substr(eq + 1));
          spdlog::warn("[ht-test] cvar {}={}", item.substr(0, eq), item.substr(eq + 1));
        }
        start = end + 1;
      }
    }
    if (!test_mode && ((rex::cvar::GetFlagByName("ht_direct_launch") != "true" && rex::cvar::GetFlagByName("ht_import_on_start") == "true") ||
                       !ht::ReadyForLaunch(library_root_))) {
      rex::cvar::SetFlagByName("fullscreen", "false");
      rex::cvar::SetFlagByName("window_width", "920");
      rex::cvar::SetFlagByName("window_height", "660");
    }

    if (!res) {
      if (f) fclose(f);
      return false;
    }

    // 4. Optional debug console.
    //
    // Off in a release build. AllocConsole gives the process a SECOND visible
    // window - a black console titled with the exe path - which looks broken
    // to a player and is the window most scripts and screenshot tools pick up
    // by mistake. Build with -DHT_DEV_CONSOLE=1 when you want the live log.
    // The rex_log.txt file sink below is always active either way, so nothing
    // is lost for bug reports.
#if HT_DEV_CONSOLE
#if defined(_WIN32)
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
#endif
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_pattern("[%l] [%n] %v");
    spdlog::default_logger()->sinks().push_back(console_sink);
#endif

    const auto logs=test_mode ? exe_dir : ht::SettingsDirectory()/"logs";
    try {
      std::filesystem::create_directories(logs);
      auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(rex::path_to_utf8(logs/"rex_log.txt"), true);
      file_sink->set_pattern("[%l] [%n] %v");
      spdlog::default_logger()->sinks().push_back(file_sink);
    } catch (const std::exception&) {
      // Read-only location: keep running without this extra log file.
    }

    // 5. Game files.
    //
    // This port ships no game content. If game_data is not there yet, walk the
    // player through importing it from their own copy - see src/ht_setup.h.
    // Does nothing once the files are in place.
    if(ht::ReadyForLaunch(library_root_))
      std::filesystem::create_directories(ht::GameDirectory(library_root_) / "DataCache");

    if (f) { fprintf(f, "SetupEnvironment complete\n"); fclose(f); }
    return true;
  }

  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults, std::function<void(rex::PathConfig)> resume) override {
    const auto root = library_root_;
    const char* testing = std::getenv("HT_BOOT_TEST");
    if ((testing && std::strcmp(testing, "1") == 0) ||
        (ht::ReadyForLaunch(root) && (rex::cvar::GetFlagByName("ht_direct_launch") == "true" || rex::cvar::GetFlagByName("ht_import_on_start") == "false")))
      return defaults;
    if (window()) window()->SetTitle("Hydro Thunder Hurricane - Configurador");
    ht::ShowSetupWizard(imgui_drawer(), defaults, std::move(resume));
    return std::nullopt;
  }
 protected:
  std::filesystem::path library_root_;
};
