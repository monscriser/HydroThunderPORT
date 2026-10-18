#pragma once

// Force-included into every generated translation unit (see CMakeLists.txt).
//
// This header used to also #define sprintf, _vsnprintf, _snprintf, vsprintf,
// swprintf, _vswprintf, RtlCaptureContext, RtlUnwind and RtlRaiseException to
// ht_* replacements. None of those ht_* symbols was ever defined anywhere in
// the tree. The renames were scaffolding for an older codegen revision that
// emitted bare CRT names; current codegen emits __imp__<name> instead, so the
// macros matched nothing in the generated sources - but they *did* apply to the
// standard headers included after them, which is a real hazard.
//
// They are gone. Do not reintroduce a rename here without a definition to go
// with it.
//
// What remains is only the platform prelude the generated sources expect.

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <cstdio>
#include <cstring>
#include <cwchar>
