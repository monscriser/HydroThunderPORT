// Minimal Win32 shim for building FidelityFX_SC (glslang path) on Linux.
#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <string>

typedef long HRESULT;
#define S_OK 0L
typedef int BOOL;
typedef unsigned long DWORD;
typedef unsigned char BYTE;
typedef wchar_t* PWSTR;
typedef const wchar_t* PCWSTR;
#define NULL_PTR nullptr
#define CP_UTF8 65001
#define PATHCCH_ALLOW_LONG_PATHS 0x1u
#define PATHCCH_ENSURE_TRAILING_SLASH 0x2u

inline std::string __shim_u8(const wchar_t* s, size_t n) {
  std::string out;
  for (size_t i = 0; i < n; ++i) {
    uint32_t c = static_cast<uint32_t>(s[i]);
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F)); }
  }
  return out;
}
inline std::wstring __shim_w(const std::string& s) {
  std::wstring out;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    uint32_t cp; int extra;
    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
    else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
    else { cp = c & 0x07; extra = 3; }
    ++i;
    for (int k = 0; k < extra && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    out += static_cast<wchar_t>(cp);
  }
  return out;
}

inline int WideCharToMultiByte(unsigned, unsigned long, const wchar_t* in, int inLen, char* out, int outLen, const char*, BOOL*) {
  std::string u8 = __shim_u8(in, static_cast<size_t>(inLen));
  if (outLen == 0) return static_cast<int>(u8.size());
  if (static_cast<size_t>(outLen) < u8.size()) return 0;
  memcpy(out, u8.data(), u8.size());
  return static_cast<int>(u8.size());
}
inline int MultiByteToWideChar(unsigned, unsigned long, const char* in, int inLen, wchar_t* out, int outLen) {
  std::wstring w = __shim_w(std::string(in, static_cast<size_t>(inLen)));
  if (outLen == 0) return static_cast<int>(w.size());
  if (static_cast<size_t>(outLen) < w.size()) return 0;
  memcpy(out, w.data(), w.size() * sizeof(wchar_t));
  return static_cast<int>(w.size());
}

inline PWSTR __shim_dup(const std::wstring& w) {
  PWSTR p = static_cast<PWSTR>(malloc((w.size() + 1) * sizeof(wchar_t)));
  memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
  return p;
}
inline void LocalFree(void* p) { free(p); }

inline HRESULT PathAllocCanonicalize(PCWSTR in, unsigned flags, PWSTR* out) {
  namespace fs = std::filesystem;
  fs::path p = fs::absolute(fs::path(__shim_u8(in, wcslen(in)))).lexically_normal();
  std::string s = p.string();
  if ((flags & PATHCCH_ENSURE_TRAILING_SLASH) && (s.empty() || s.back() != '/')) s += '/';
  *out = __shim_dup(__shim_w(s));
  return S_OK;
}
inline HRESULT PathCchSkipRoot(PCWSTR path, PWSTR* out) {
  // Skip a leading '/' root (Linux analogue of the drive/UNC root logic).
  PCWSTR p = path;
  while (*p == L'/') ++p;
  *out = const_cast<PWSTR>(p);
  return S_OK;
}
inline HRESULT PathAllocCombine(PCWSTR a, PCWSTR b, unsigned, PWSTR* out) {
  namespace fs = std::filesystem;
  fs::path combined = fs::path(__shim_u8(a, wcslen(a))) / fs::path(__shim_u8(b, wcslen(b)));
  *out = __shim_dup(__shim_w(combined.lexically_normal().string()));
  return S_OK;
}
inline BOOL CreateDirectoryW(PCWSTR path, void*) {
  std::error_code ec;
  std::filesystem::create_directory(std::filesystem::path(__shim_u8(path, wcslen(path))), ec);
  return ec ? 0 : 1;
}
inline int _wfopen_s(FILE** fp, const wchar_t* path, const wchar_t* mode) {
  *fp = fopen(__shim_u8(path, wcslen(path)).c_str(), __shim_u8(mode, wcslen(mode)).c_str());
  return *fp ? 0 : errno;
}
