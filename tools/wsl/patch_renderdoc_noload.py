#!/usr/bin/env python3
"""RenderDoc: only attach when RenderDoc is ALREADY in the process.

Bug (user report, Steam Deck): a black text overlay "Capturing Vulkan ... F12 or
Print Screen to capture" stuck in the top-left corner with no way to hide it.
That overlay is RenderDoc's own in-app overlay.

Cause: RenderDocAPI::CreateIfConnected() did a plain dlopen("librenderdoc.so").
The comment says "if RenderDoc is attached the library is already loaded", but a
plain dlopen LOADS the library when it exists anywhere on the library path -
and SteamOS ships /usr/lib/librenderdoc.so. Loading it injects RenderDoc into
the game: its Vulkan hooks, its overlay, and (flags.cpp) GPU debug markers get
auto-enabled, which also costs performance. Upstream Xenia uses RTLD_NOLOAD /
GetModuleHandle for exactly this reason.

Fix: Linux -> dlopen(RTLD_NOW | RTLD_NOLOAD) probe first; Windows -> the module
must already be loaded (GetModuleHandleW). Only then take a reference.
usage: patch_renderdoc_noload.py <sdk root>
"""
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
f = root / "src/ui/renderdoc_api.cpp"
s = f.read_text()
MARK = "ht: RenderDoc attach-only"
if MARK in s:
    print("renderdoc_api.cpp: already patched")
    sys.exit(0)

old_inc = "#include <rex/ui/renderdoc_api.h>\n"
new_inc = (old_inc +
           "\n#if REX_PLATFORM_LINUX || REX_PLATFORM_ANDROID\n#include <dlfcn.h>\n#endif\n"
           "#if REX_PLATFORM_WIN32\n#ifndef NOMINMAX\n#define NOMINMAX\n#endif\n#include <windows.h>\n#endif\n")
assert s.count(old_inc) == 1
s = s.replace(old_inc, new_inc)

old = "  if (!renderdoc_api->library_.Load(platform::lib_names::kRenderDoc)) {\n"
assert s.count(old) == 1, "load anchor not found"
new = (
    "  // " + MARK + ": never LOAD RenderDoc ourselves. A plain dlopen/LoadLibrary\n"
    "  // pulls in a system-wide librenderdoc.so (SteamOS ships one) and shows its\n"
    "  // \"Capturing Vulkan\" overlay to every player. Only take a reference when\n"
    "  // RenderDoc already injected itself (launched from the RenderDoc UI).\n"
    "#if REX_PLATFORM_LINUX || REX_PLATFORM_ANDROID\n"
    "  if (void* already = dlopen(platform::lib_names::kRenderDoc, RTLD_NOW | RTLD_NOLOAD)) {\n"
    "    dlclose(already);\n"
    "  } else {\n"
    "    return nullptr;\n"
    "  }\n"
    "#elif REX_PLATFORM_WIN32\n"
    "  if (!GetModuleHandleA(platform::lib_names::kRenderDoc)) {\n"
    "    return nullptr;\n"
    "  }\n"
    "#endif\n"
    + old)
s = s.replace(old, new)
f.write_text(s)
print("renderdoc_api.cpp: patched (attach-only)")
