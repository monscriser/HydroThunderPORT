#!/usr/bin/env python3
"""1.2.0: launcher options "pre-compile shaders before the game opens" and "write log files".

Shader pre-compilation: the SDK already creates every pipeline stored in the cache before the
first frame (InitializeShaderStorage(blocking=true) at module launch, ~7.6 s on the ROG Ally),
but it ran INSIDE the UI thread, so the window stayed black and frozen. Now:
  - the storage load runs on a worker thread; the UI thread keeps painting, and the module
    starts (OnPostLaunchModule + Resume) from the UI thread when it is done;
  - progress (pipelines created / total) is published through rex::ht_progress for an overlay;
  - ht_shader_precompile=false skips creating the stored pipelines up front (faster start; each
    pipeline is then created the first time it is drawn, like a brand-new one).
Logs: ht_write_logs=false creates no log file (hydrothunder_NNN.log).
usage: patch_shader_precompile_logs.py <sdk root>
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
MARK = "ht_progress"


def patch(rel, pairs):
    f = root / rel
    s = f.read_text()
    if MARK in s:
        print(f"{rel}: already patched")
        return
    for old, new in pairs:
        assert s.count(old) == 1, f"{rel}: anchor {old[:70]!r} x{s.count(old)}"
        s = s.replace(old, new)
    f.write_text(s)
    print(f"{rel}: patched")


(root / "include/rex/ht_progress.h").write_text(
    "// ht_progress: start-up shader pre-compilation progress (defined in src/ui/rex_app.cpp).\n"
    "#pragma once\n#include <cstdint>\n\nnamespace rex::ht_progress {\n"
    "struct ShaderPreload {\n  uint32_t done = 0, total = 0;\n  bool active = false;   // loading/creating now\n"
    "  bool finished = false;  // start-up pre-compilation over (or skipped)\n};\n"
    "void SetShaderPreload(uint32_t done, uint32_t total);\n"
    "void BeginShaderPreload();\nvoid FinishShaderPreload();\nShaderPreload GetShaderPreload();\n"
    "}  // namespace rex::ht_progress\n")
print("include/rex/ht_progress.h: written")

patch("src/ui/rex_app.cpp", [
    ("#include <rex/version.h>\n",
     "#include <rex/version.h>\n#include <rex/ht_progress.h>\n#include <atomic>\n#include <memory>\n#include <thread>\n"),
    ("namespace rex {\n",
     "REXCVAR_DEFINE_BOOL(ht_write_logs, false, \"Log\", \"Write log files (hydrothunder_NNN.log); off by default\");\n"
     "REXCVAR_DEFINE_BOOL(ht_shader_precompile, true, \"GPU\",\n"
     "                    \"Create every cached pipeline before the game starts (with a progress screen)\");\n\n"
     "namespace rex {\n"),
    ("  auto log_config = rex::BuildLogConfig(log_file_cvar.empty() ? nullptr : log_file_cvar.c_str(),\n"
     "                                        log_level_str, category_levels);\n"
     "  if (log_file_cvar.empty()) {\n",
     "  // ht_write_logs=false (launcher option): no log file at all.\n"
     "  const bool ht_logs = REXCVAR_GET(ht_write_logs);\n"
     "  auto log_config = rex::BuildLogConfig(\n"
     "      (log_file_cvar.empty() || !ht_logs) ? nullptr : log_file_cvar.c_str(), log_level_str,\n"
     "      category_levels);\n"
     "  if (log_file_cvar.empty() && ht_logs) {\n"),
    ("    auto* graphics_system = runtime_->graphics_system();\n"
     "    if (graphics_system && !runtime_->cache_root().empty()) {\n"
     "      uint32_t title_id = runtime_->kernel_state()->title_id();\n"
     "      if (title_id != 0) {\n"
     "        REXLOG_INFO(\"Initializing shader storage for title {:08X}...\", title_id);\n"
     "        graphics_system->InitializeShaderStorage(runtime_->cache_root(), title_id, true);\n"
     "      }\n"
     "    }\n\n"
     "    OnPostLaunchModule(main_thread.get());\n"
     "    main_thread->Resume();\n\n"
     "    module_thread_ = std::thread([this, main_thread = std::move(main_thread)]() mutable {\n"
     "      main_thread->Wait(0, 0, 0, nullptr);\n"
     "      OnGuestThreadExit(main_thread.get());\n"
     "      REXLOG_INFO(\"Execution complete\");\n"
     "      if (!shutting_down_.load(std::memory_order_acquire)) {\n"
     "        app_context().CallInUIThread([this]() { app_context().QuitFromUIThread(); });\n"
     "      }\n"
     "    });\n",
     "    // ht_progress: the cached pipelines are created on a worker thread so the window keeps\n"
     "    // painting (progress overlay); the module starts from the UI thread afterwards.\n"
     "    auto main_holder = std::make_shared<decltype(main_thread)>(std::move(main_thread));\n"
     "    auto start_module = [this, main_holder]() {\n"
     "      if (shutting_down_.load(std::memory_order_acquire)) return;\n"
     "      OnPostLaunchModule(main_holder->get());\n"
     "      (*main_holder)->Resume();\n"
     "      module_thread_ = std::thread([this, main_holder]() {\n"
     "        (*main_holder)->Wait(0, 0, 0, nullptr);\n"
     "        OnGuestThreadExit(main_holder->get());\n"
     "        REXLOG_INFO(\"Execution complete\");\n"
     "        if (!shutting_down_.load(std::memory_order_acquire)) {\n"
     "          app_context().CallInUIThread([this]() { app_context().QuitFromUIThread(); });\n"
     "        }\n"
     "      });\n"
     "    };\n"
     "    auto* graphics_system = runtime_->graphics_system();\n"
     "    uint32_t title_id = runtime_->kernel_state() ? runtime_->kernel_state()->title_id() : 0;\n"
     "    if (graphics_system && !runtime_->cache_root().empty() && title_id != 0) {\n"
     "      REXLOG_INFO(\"Initializing shader storage for title {:08X}...\", title_id);\n"
     "      rex::ht_progress::BeginShaderPreload();\n"
     "      std::thread([this, graphics_system, cache_root = runtime_->cache_root(), title_id,\n"
     "                   start_module]() {\n"
     "        graphics_system->InitializeShaderStorage(cache_root, title_id, true);\n"
     "        rex::ht_progress::FinishShaderPreload();\n"
     "        app_context().CallInUIThread(start_module);\n"
     "      }).detach();\n"
     "    } else {\n"
     "      rex::ht_progress::FinishShaderPreload();\n"
     "      start_module();\n"
     "    }\n"),
])

# The progress state lives in librexruntime: the GPU plugin links it, while rex_app.cpp is in the exe.
patch("src/filesystem/virtual_file_system.cpp", [
    ("#include <rex/filesystem/vfs.h>\n",
     "#include <rex/filesystem/vfs.h>\n#include <rex/ht_progress.h>\n#include <atomic>\n"
     "namespace rex::ht_progress {\n"
     "static std::atomic<uint32_t> g_done{0}, g_total{0};\n"
     "static std::atomic<bool> g_active{false}, g_finished{false};\n"
     "void SetShaderPreload(uint32_t done, uint32_t total) { g_total.store(total); g_done.store(done); }\n"
     "void BeginShaderPreload() { g_done = 0; g_total = 0; g_finished = false; g_active = true; }\n"
     "void FinishShaderPreload() { g_active = false; g_finished = true; }\n"
     "ShaderPreload GetShaderPreload() {\n"
     "  ShaderPreload s; s.done = g_done.load(); s.total = g_total.load(); s.active = g_active.load();\n"
     "  s.finished = g_finished.load(); return s;\n}\n"
     "}  // namespace rex::ht_progress\n"),
])

patch("src/graphics/vulkan/pipeline_cache.cpp", [
    ("#include <rex/graphics/vulkan/pipeline_cache.h>\n",
     "#include <rex/graphics/vulkan/pipeline_cache.h>\n#include <rex/ht_progress.h>  // ht_progress\n"),
    ("  if (!pipeline_creations.empty()) {\n"
     "    {\n"
     "      std::lock_guard<std::mutex> lock(creation_request_lock_);\n"
     "      startup_loading_ = true;\n"
     "    }\n",
     "  // ht_shader_precompile=false (launcher option): do not create the stored pipelines up\n"
     "  // front; each one is created the first time it is drawn, like a new pipeline.\n"
     "  if (rex::cvar::GetFlagByName(\"ht_shader_precompile\") == \"false\" && !pipeline_creations.empty()) {\n"
     "    REXGPU_WARN(\"[ht-shaders] pre-compilation off: {} cached pipelines will be created on first use\",\n"
     "                pipeline_creations.size());\n"
     "    pipeline_creations.clear();\n"
     "  }\n"
     "  rex::ht_progress::SetShaderPreload(0, uint32_t(pipeline_creations.size()));\n"
     "  if (!pipeline_creations.empty()) {\n"
     "    {\n"
     "      std::lock_guard<std::mutex> lock(creation_request_lock_);\n"
     "      startup_loading_ = true;\n"
     "    }\n"),
    ("        if (EnsurePipelineCreated(pipeline_creations[creation_index])) {\n"
     "          created_pipeline_count.fetch_add(1);\n"
     "        }\n",
     "        if (EnsurePipelineCreated(pipeline_creations[creation_index])) {\n"
     "          created_pipeline_count.fetch_add(1);\n"
     "        }\n"
     "        rex::ht_progress::SetShaderPreload(uint32_t(creation_index + 1),\n"
     "                                           uint32_t(pipeline_creations.size()));\n"),
    ("    REXGPU_INFO(\"Created {} graphics pipelines from Vulkan storage ({} requested)\",\n",
     "    REXGPU_WARN(\"[ht-shaders] pre-compiled {} of {} cached pipelines before the game started\",\n"
     "                created_pipeline_count.load(), pipeline_creations.size());\n"
     "    REXGPU_INFO(\"Created {} graphics pipelines from Vulkan storage ({} requested)\",\n"),
])
