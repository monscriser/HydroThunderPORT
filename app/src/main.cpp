// htrecomp - Hydro Thunder Hurricane, ReXGlue recompiled project
//
// This file is yours to edit. 'rexglue migrate' will NOT overwrite it.

#include "hydrothunder_init.h"
#include "ht_app_common.h"
#include <rex/input/input_system.h>
#include "ht_boot_test.h"

#include "ht_fps_overlay.h"
#include "ht_settings_overlay.h"
#include "ht_owned_content_labels.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/cvar.h>
#include <rex/runtime.h>
#include <rex/system/xnotifylistener.h>
extern "C" void rex_original_XamShowMarketplaceUI(PPCContext&, uint8_t*);
extern "C" void rex_original_XGetLanguage(PPCContext&, uint8_t*);
#include <rex/ui/keybinds.h>
#include <imgui.h>

// NVIDIA Optimus / AMD PowerXpress discrete-GPU opt-in. The drivers only read
// these from the executable's export table; copies inside rexruntime.dll are
// invisible to them, which leaves hybrid-graphics laptops rendering on the
// integrated GPU. They must live here, in the exe image itself.
#if defined(_WIN32)
#include <windows.h>
#include <timeapi.h>
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 1;
}
#endif

#if defined(_WIN32)
namespace {

// Last-resort crash reporter.
//
// Installed from OnPreSetup so it covers runtime construction and XEX load, not
// just guest execution. A guest fault surfaces here as an access violation whose
// RIP lands inside a recompiled sub_XXXXXXXX; the shipped PDB resolves it, so the
// faulting guest address is recoverable from the report:
//
//   llvm-symbolizer.exe --obj=hydrothunder.exe <rip - live base + 0x140000000>
LONG WINAPI HtUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
  if (!info || !info->ExceptionRecord || !info->ContextRecord) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  auto path = rex::filesystem::GetExecutableFolder() / "crash_dump.txt";
  std::ofstream out(path, std::ios::app);
  if (!out) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  const auto* rec = info->ExceptionRecord;
  const auto* ctx = info->ContextRecord;

  out << std::hex << std::uppercase;
  out << "--- htrecomp crash ---\n";
  out << "code    0x" << rec->ExceptionCode << "\n";
  out << "address 0x" << rec->ExceptionAddress << "\n";
  if (rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
      rec->NumberParameters >= 2) {
    static const char* kAccess[] = {"read", "write", nullptr, nullptr,
                                    nullptr, nullptr, nullptr, "execute"};
    const auto kind = rec->ExceptionInformation[0];
    const char* what = (kind < 8 && kAccess[kind]) ? kAccess[kind] : "?";
    out << "access  " << what << " of 0x" << rec->ExceptionInformation[1] << "\n";
  }
  out << "rip " << ctx->Rip << "  rsp " << ctx->Rsp << "  rbp " << ctx->Rbp << "\n";
  out << "rax " << ctx->Rax << "  rbx " << ctx->Rbx << "  rcx " << ctx->Rcx << "\n";
  out << "rdx " << ctx->Rdx << "  rsi " << ctx->Rsi << "  rdi " << ctx->Rdi << "\n";
  out << "\n";
  out.flush();

  return EXCEPTION_EXECUTE_HANDLER;
}

// First-chance exception logger.
//
// SetUnhandledExceptionFilter is not enough on its own: it only runs for
// exceptions nothing else handles, and never for __fastfail (which is how
// STATUS_HEAP_CORRUPTION aborts). A vectored handler registered first sees every
// exception before any SEH frame or handler inside the recompiled code can
// swallow it. That is how the boot crash was found, and it costs nothing while
// nothing is throwing.
//
// Levels are deliberately low: a first-chance exception is often normal guest
// behaviour, so this must not read as a fault.
LONG CALLBACK HtVectoredHandler(EXCEPTION_POINTERS* info) {
  if (!info || !info->ExceptionRecord) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const DWORD code = info->ExceptionRecord->ExceptionCode;

  // Noise that is normal and not worth a line each time.
  switch (code) {
    case 0x406D1388:            // SetThreadName marker
    case DBG_PRINTEXCEPTION_C:  // OutputDebugString
    case 0x4001000A:            // DBG_PRINTEXCEPTION_WIDE_C
      return EXCEPTION_CONTINUE_SEARCH;
    default:
      break;
  }

  const void* addr = info->ExceptionRecord->ExceptionAddress;
  const char* kind = "";
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:    kind = " ACCESS_VIOLATION"; break;
    case 0xE06D7363:                    kind = " C++ exception";    break;
    case 0xC0000374:                    kind = " HEAP_CORRUPTION";  break;
    case EXCEPTION_STACK_OVERFLOW:      kind = " STACK_OVERFLOW";   break;
    case EXCEPTION_ILLEGAL_INSTRUCTION: kind = " ILLEGAL_INSN";     break;
    default: break;
  }

  if (code == EXCEPTION_ACCESS_VIOLATION &&
      info->ExceptionRecord->NumberParameters >= 2) {
    // An AV is worth surfacing even when something downstream handles it.
    REXLOG_WARN("[ht] VEH tid={} code=0x{:08X}{} at {} - {} of 0x{:X}",
                ::GetCurrentThreadId(), static_cast<uint32_t>(code), kind, addr,
                info->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                static_cast<uint64_t>(info->ExceptionRecord->ExceptionInformation[1]));
    // Name the module and offset of the fault and of a few callers, so a crash
    // inside a DLL can be located without a debugger.
    auto describe = [](DWORD64 pc) {
      HMODULE module = nullptr;
      char name[MAX_PATH] = "?";
      if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                 GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             reinterpret_cast<LPCSTR>(pc), &module) &&
          module) {
        char path[MAX_PATH];
        if (GetModuleFileNameA(module, path, MAX_PATH)) {
          const char* base = strrchr(path, '\\');
          strncpy(name, base ? base + 1 : path, MAX_PATH - 1);
          name[MAX_PATH - 1] = 0;
        }
      }
      return fmt::format("{}+0x{:X}", name,
                         module ? pc - reinterpret_cast<DWORD64>(module) : pc);
    };
    if (info->ContextRecord) {
      CONTEXT ctx = *info->ContextRecord;
      std::string frames;
      for (int frame = 0; frame < 12 && ctx.Rip; ++frame) {
        frames += (frame ? " <- " : "") + describe(ctx.Rip);
        DWORD64 image_base = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(ctx.Rip, &image_base, nullptr);
        if (!function) {
          // Leaf function: the return address is on top of the stack.
          if (!ctx.Rsp || IsBadReadPtr(reinterpret_cast<void*>(ctx.Rsp), 8)) break;
          ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
          ctx.Rsp += 8;
          continue;
        }
        void* handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip, function, &ctx, &handler_data,
                         &establisher, nullptr);
      }
      REXLOG_WARN("[ht] VEH stack: {}", frames);
    }
  } else {
    REXLOG_DEBUG("[ht] VEH tid={} code=0x{:08X}{} at {}", ::GetCurrentThreadId(),
                 static_cast<uint32_t>(code), kind, addr);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace
#endif

// ---------------------------------------------------------------------------
// Guest-function tracing.
//
// Development tooling, off by default. Flip to 1 to pull in the hooks that
// found the boot bug (see src/ht_debug_hooks.h and investigation-map.md).
// Leave it at 0 for anything you intend to play: every hook logs synchronously
// on the guest thread and the volume is enough to hurt frame pacing, audio
// continuity and input latency.
// ---------------------------------------------------------------------------
#define HT_TRACE_GUEST_ENTRY 0

#if HT_TRACE_GUEST_ENTRY
#include "ht_debug_hooks.h"
#endif

// CPU: a real pause in the game's spin-wait step (see the header).
#include "ht_spin_backoff.h"

REXCVAR_DEFINE_BOOL(ht_guest_exit_hard, true, "Hydro Thunder",
                    "When the game quits by itself, exit like a window close (skip the crashing teardown)");

class HtPureApp : public HtBaseApp {
 public:
  using HtBaseApp::HtBaseApp;

  bool SetupEnvironment() override {
#if defined(_WIN32)
    // Also cover presentation setup, which happens before runtime construction.
    SetUnhandledExceptionFilter(HtUnhandledExceptionFilter);
    AddVectoredExceptionHandler(1,HtVectoredHandler);
#endif
    return HtBaseApp::SetupEnvironment();
  }

  std::string_view GetBuildTitle() const override {
    return HT_BUILD_TITLE;
  }

  std::string_view GetBuildStamp() const override {
    return HT_BUILD_STAMP;
  }

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<HtPureApp>(
        new HtPureApp(ctx, "hydrothunder", PPCImageConfig));
  }

 protected:
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    ImFontConfig config;
    config.SizePixels = 18.0f;
    const auto path = rex::filesystem::GetExecutableFolder() / "ui" / "Karla-Regular.ttf";
    ImFont* font = std::filesystem::exists(path)
        ? atlas->AddFontFromFileTTF(path.string().c_str(), 18.0f, &config)
        : atlas->AddFontDefault(&config);
    if (font) ImGui::GetIO().FontDefault = font;
  }

  void OnConfigureStyle(ImGuiStyle& style, rex::ui::Style&) override {
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(22, 18);
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(10, 10);
    style.WindowRounding = 6;
    style.FrameRounding = 4;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(.059f,.098f,.153f,1);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(.05f,.24f,.37f,1);
    style.Colors[ImGuiCol_Button] = ImVec4(.13f,.22f,.30f,1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(.18f,.34f,.45f,1);
    style.Colors[ImGuiCol_Tab] = ImVec4(.059f,.098f,.153f,1);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(.09f,.16f,.23f,1);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(.12f,.24f,.32f,1);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(.3f,.78f,.96f,1);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(.05f,.55f,.76f,1);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(.25f,.78f,1,1);
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    config.gpu_plugin = "xenos";
#if defined(_WIN32)
    // Raise the process timer resolution to 1 ms.
    //
    // The guest vblank generator (graphics_system.cpp, "GPU VSync" thread)
    // paces itself with Sleep(1ms). Windows' default timer resolution is
    // ~15.6 ms, so that sleep overshoots badly and guest vblanks arrive
    // irregularly - measured gaps of up to 32 ms against a 16.67 ms target.
    // Nothing else in the process raises it: the SDL audio driver explicitly
    // sets SDL_HINT_TIMER_RESOLUTION to "0", and timeBeginPeriod is called
    // nowhere in the SDK.
    //
    // This was found because maximising the window made the audio stutter
    // disappear - a window resize makes DWM raise the system timer resolution,
    // which accidentally fixed the vblank pacing.
    if (timeBeginPeriod(1) != TIMERR_NOERROR) {
      REXLOG_WARN("[ht] timeBeginPeriod(1) failed; guest vblank pacing will be coarse");
    }

    // Needed to turn a faulting RIP back into a guest function via the PDB.
    REXLOG_DEBUG("[ht] host image base = {}", fmt::ptr(::GetModuleHandleW(nullptr)));
#endif
    HtBaseApp::OnPreSetup(config);
  }

  void OnPostLaunchModule(rex::system::XThread* thread) override {
    HtBaseApp::OnPostLaunchModule(thread);

    // The release window title.
    //
    // Set here rather than by renaming the app, because GetName() also builds
    // the config filename (rex_app.cpp:112) - and the release title contains a
    // colon, which is illegal in a Windows path. The SDK sets its own title
    // during setup (rex_app.cpp:291), so this has to run afterwards; this hook
    // does, and window() is public.
    if (auto* win = window()) {
      win->SetTitle(HT_WINDOW_TITLE);
    }

    // Suspend mouse-and-keyboard controller emulation while the settings panel
    // is open.
    //
    // With mnk_mode on, MnkInputDriver::UpdateMouseCapture hides the cursor and
    // re-centres it EVERY FRAME (mnk_input_driver.cpp:296). The pointer can
    // therefore never rest on a widget, so the panel looks like it ignores the
    // mouse entirely. InputDriver::is_active() exists for exactly this and
    // nothing in the SDK ever supplies it - InputSystem::SetActiveCallback has
    // no callers - so the default is "always active".
    //
    // Suspending also stops keystrokes reaching the game as controller input
    // while a text box has focus, which is wanted in its own right: typing a
    // window width should not steer the boat.
    // Persist installed DLC metadata and its full configured entitlement before
    // the guest starts, so re-opening the app doesn't require marketplace UI.
    auto* content = runtime()->kernel_state()->content_manager();
    const auto packages = content->ListContent(
        1, 0, rex::system::XContentType::kMarketplaceContent, 0x5841096A);
    REXLOG_WARN("[ht-content] Full XBLA license active; update 0.0.2.1; {} installed DLC packages", packages.size());
    bool installed_content_owned=!packages.empty();
    for (const auto& package : packages) {
      const auto header_result = content->WriteContentHeaderFile(0, package, 0xFFFFFFFFu);
      uint32_t license = 0;
      const auto open_result = content->OpenContent("ht-installed-content-check", 0, package, license);
      REXLOG_WARN("[ht-content] {} header={:08X} open={:08X} license={:08X}",
                  package.file_name(), uint32_t(header_result), uint32_t(open_result), license);
      installed_content_owned &= header_result == 0 && open_result == 0 && license == 0xFFFFFFFFu;
      if (open_result == 0) content->CloseContent("ht-installed-content-check");
    }
    const auto content_root=library_root_;
    bool content_caption_active=false;
    // pt-BR texts apply whether or not the DLC is owned; the "purchased" caption only when it is.
    if(ht::ReadyForLaunch(content_root))
      content_caption_active=ht::InstallOwnedContentLabels(runtime(),content_root,installed_content_owned) &&
                             installed_content_owned;

    if (const char* flow_test = std::getenv("HT_CONTENT_FLOW_TEST");
        flow_test && std::strcmp(flow_test, "1") == 0) {
      auto* kernel = runtime()->kernel_state();
      auto listener = rex::system::object_ref<rex::system::XNotifyListener>(
          new rex::system::XNotifyListener(kernel));
      listener->Initialize(3, 10);
      uint32_t id = 0, data = 0;
      bool startup_owned = false;
      while (listener->DequeueNotification(&id, &data))
        startup_owned |= id == 0x02000007;
      PPCContext call{};
      call.r3.u64 = 0;
      call.r4.u64 = 1;
      call.r5.u64 = (uint64_t(0x5841096A) << 32) | 1;
      call.r6.u64 = 0xFFFFFFFF;
      rex_original_XamShowMarketplaceUI(call, runtime()->memory()->virtual_membase());
      bool saw_open = false, saw_owned = false, saw_closed = false;
      while (listener->DequeueNotification(&id, &data)) {
        saw_open |= id == 9 && data == 1;
        saw_owned |= id == 0x02000007;
        saw_closed |= id == 9 && data == 0;
      }
      PPCContext language_call{};
      rex_original_XGetLanguage(language_call, runtime()->memory()->virtual_membase());
      const auto configured_language=std::stoul(rex::cvar::GetFlagByName("user_language"));
      const bool passed = language_call.r3.u32 == configured_language &&
                          call.r3.u32 == 0 && startup_owned && saw_open && saw_owned && saw_closed &&
                          rex::cvar::GetFlagByName("license_mask") == "4294967295";
      kernel->UnregisterNotifyListener(listener.get());
      std::ofstream result(rex::filesystem::GetExecutableFolder() / "content-flow-test.json");
      result << "{\"passed\":" << (passed ? "true" : "false")
             << ",\"startupOwned\":" << (startup_owned ? "true" : "false")
             << ",\"uiOpened\":" << (saw_open ? "true" : "false")
             << ",\"contentInstalled\":" << (saw_owned ? "true" : "false")
             << ",\"uiClosed\":" << (saw_closed ? "true" : "false")
             << ",\"returnCode\":" << call.r3.u32
             << ",\"gameLanguage\":" << language_call.r3.u32
             << ",\"contentCaptionActive\":" << (content_caption_active ? "true" : "false") << "}";
      result.flush();
      REXLOG_WARN("[ht-content-test] passed={} startup={} open={} installed={} closed={} result={:08X}",
                  passed, startup_owned, saw_open, saw_owned, saw_closed, call.r3.u32);
      std::exit(passed ? 0 : 20);
    }

    auto* input = dynamic_cast<rex::input::InputSystem*>(runtime()->input_system());
    if (input) {
      input->SetActiveCallback([] { return !HtSettingsPanelOpen(); });
      const char* boot_test = std::getenv("HT_BOOT_TEST");
      if (boot_test && std::strcmp(boot_test, "1") == 0) {
        input->AddDriver(std::make_unique<rex::input::HtBootTestDriver>(0));
        input->AddDriver(std::make_unique<rex::input::HtBootTestDriver>(1));
        auto* kernel = runtime()->kernel_state();
        auto* p1 = kernel->user_profile(0);
        auto* p2 = kernel->user_profile(1);
        if (!p1 || !p2 || p1->xuid() == p2->xuid() ||
            p1->name() != "Player 1" || p2->name() != "Player 2" ||
            !p1->signin_state() || !p2->signin_state() || kernel->user_profile(2))
          throw std::runtime_error("Local player profile validation failed");
        auto* content = kernel->content_manager();
        if (content->ResolveGameUserContentPath(p1->xuid()) ==
            content->ResolveGameUserContentPath(p2->xuid()))
          throw std::runtime_error("Local player storage is not independent");
        REXLOG_WARN("[ht-boot-test] Player 1 / Player 2 identities and independent storage verified");
          const auto packages = content->ListContent(
              1, 0, rex::system::XContentType::kMarketplaceContent, 0x5841096A);
          for (const auto& package : packages) {
            uint32_t mask = 0;
            const auto result = content->OpenContent("ht-license-probe", 0, package, mask);
            REXLOG_WARN("[ht-boot-test] DLC {} result={:08X} mask={:08X} configured={}", package.file_name(), uint32_t(result), mask, rex::cvar::GetFlagByName("license_mask"));
            if (result != 0 || mask != 0xFFFFFFFFu)
              throw std::runtime_error("Installed DLC license validation failed");
            content->CloseContent("ht-license-probe");
            REXLOG_WARN("[ht-boot-test] DLC {} opened with license FFFFFFFF", package.file_name());
          }
        REXLOG_WARN("[ht-boot-test] integration input enabled");
      }
    }
  }

  void OnShutdown() override {
    HtBaseApp::OnShutdown();
  }

  // The game quitting by itself (XamLoaderTerminateTitle) makes the SDK leave the
  // message loop and run the full teardown in ReXApp::OnDestroy, which destroys
  // the window before the runtime; ~MnkInputDriver then detaches from the freed
  // window and the heap check aborts (0xC0000374, seen in every crash dump since
  // revision 2). Closing the window never reaches that path: ReXApp::OnClosing
  // flushes the log and hard-exits. Do the same here (ht_guest_exit_hard).
  void OnGuestThreadExit(rex::system::XThread* thread) override {
    HtBaseApp::OnGuestThreadExit(thread);
    if (!REXCVAR_GET(ht_guest_exit_hard)) return;
    REXLOG_WARN("[ht-exit] game exited by itself; hard-exiting like a window close");
    rex::FlushLogging();
    std::_Exit(0);
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    HtBaseApp::OnConfigurePaths(paths);
    config_path_ = paths.config_path;
    user_data_root_ = paths.user_data_root;
  }

  // Ignore auto-repeat for overlay keybinds.
  //
  // Windows sends repeated WM_KEYDOWN while a key is held, and the SDK's
  // rex::ui::ProcessKeyEvent fires a bind's callback on every one of them
  // without consulting KeyEvent::prev_state(). Every overlay bind is a toggle,
  // so a single held keypress flips the overlay open/closed several times and
  // an even count leaves it closed - which looks exactly like the keybind not
  // working at all. Measured: one F5 press produced four toggles.
  //
  // ReXApp::OnKeyDown does nothing but call ProcessKeyEvent, so filtering
  // repeats here fixes every bind (F3 debug, F4 settings, backtick console, F5)
  // without patching the SDK. Guest input is unaffected: it arrives through the
  // MnK driver's own listener, not this one.
  void OnKeyDown(rex::ui::KeyEvent& e) override {
    if (e.prev_state()) {
      return;
    }
    // ReXApp::OnKeyDown is private, so call what it calls. Overriding a private
    // virtual is fine; only invoking the base version is not.
    rex::ui::ProcessKeyEvent(e);
  }

  // Player-facing settings panel, on F5. The SDK's own dialog stays on F4 as
  // the advanced view - it lists every registered cvar by raw name, which is
  // the right tool for debugging and the wrong one for playing.
  //
  // This hangs off the SDK's documented extension point rather than patching
  // the SDK, so it survives an SDK upgrade untouched. See SDK_PATCHES.md.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    if (const char* test = std::getenv("HT_BOOT_TEST"); test && std::string_view(test) == "1") {
      rex::ui::RegisterBind("bind_ht_portal_test", "F9", "Toggle portal depth test (validation)", [] {
        const auto previous = rex::cvar::GetFlagByName("force_additive_depth_test");
        const bool enabled = previous != "true" && previous != "1";
        rex::cvar::SetFlagByName("force_additive_depth_test", enabled ? "true" : "false");
        REXLOG_WARN("[ht-portal-ab] depth workaround={}", enabled);
      });
    }
    auto toggle_fps = [this, drawer](bool on) {
      if (on && !ht_fps_) {
        ht_fps_ = std::make_unique<HtFpsOverlay>(drawer);
      } else if (!on) {
        ht_fps_.reset();
      }
    };

    rex::ui::RegisterBind("bind_ht_settings", "F5", "Toggle Hydro Thunder settings",
                          [this, drawer, toggle_fps] {
                            if (ht_settings_) {
                              ht_settings_.reset();
                            } else {
                              ht_settings_ = std::make_unique<HtSettingsDialog>(
                                  drawer, config_path_, user_data_root_, toggle_fps);
                            }
                          });

    rex::ui::RegisterBind("bind_ht_fps", "F6", "Toggle frame rate overlay", [this, toggle_fps] {
      const bool on = !ht_fps_;
      rex::cvar::SetFlagByName("ht_show_fps", on ? "true" : "false");
      toggle_fps(on);
    });

    // Restore the overlay if it was on when the config was written.
    if (rex::cvar::GetFlagByName("ht_show_fps") == "true" || ht::lossless::session_status.requested) {
      toggle_fps(true);
    }

    HtBaseApp::OnCreateDialogs(drawer);
  }

 private:
  std::filesystem::path config_path_;
  std::filesystem::path user_data_root_;
  std::unique_ptr<HtSettingsDialog> ht_settings_;
  std::unique_ptr<HtFpsOverlay> ht_fps_;
};

REX_DEFINE_APP(hydrothunder, HtPureApp::Create)

// ---------------------------------------------------------------------------
// Kept as a note so it is not reinvented.
//
// An earlier revision hooked the guest entry point from OnPreLaunchModule by
// rewriting PPCImageConfig.func_mappings in place. That could never work:
// func_mappings is consumed by Runtime::Setup() (runtime.cpp:185), which runs
// during ConstructRuntime, well before LaunchModule() calls OnPreLaunchModule()
// (rex_app.cpp:346-348). By the time the hook ran, the dispatcher table had
// already been built from the array. The hook also targeted 0x821201D0, which is
// not the entry point - both the XEX ENTRY_POINT header and the PE
// AddressOfEntryPoint give 0x823EC9F8, generated as xstart.
//
// To hook a guest function, pick one of:
//   * define a strong symbol with REX_HOOK_RAW - this overrides codegen's weak
//     __imp__ alias and is the ONLY option that catches direct calls
//     (see src/ht_debug_hooks.h);
//   * edit PPCFuncMappings[] before Runtime::Setup() consumes it;
//   * override OnPostLaunchModule(XThread*), which runs while the main guest
//     thread is still suspended and is the SDK's documented attach point.
// ---------------------------------------------------------------------------

