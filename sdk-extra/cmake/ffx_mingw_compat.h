// Compatibility shim for building the AMD FidelityFX SDK with clang-based MinGW.
// FidelityFX uses a few MSVC CRT spellings; force-included only for the FFX subdirectory.
#pragma once
#include <stdlib.h>
#include <wchar.h>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef swprintf_s
#define swprintf_s(buffer, size, ...) swprintf((buffer), (size), __VA_ARGS__)
#endif

// MSVC pulls these in implicitly through its own headers; clang-MinGW does not.
#ifdef __cplusplus
#include <mutex>
#include <new>
#endif
