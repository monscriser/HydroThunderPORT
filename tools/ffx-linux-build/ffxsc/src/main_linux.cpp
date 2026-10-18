#include "winshim.h"
int wmain(int argc, wchar_t** argv);
int main(int argc, char** argv) {
  std::wstring* store = new std::wstring[argc];
  wchar_t** wargv = new wchar_t*[argc];
  for (int i = 0; i < argc; ++i) { store[i] = __shim_w(argv[i]); wargv[i] = const_cast<wchar_t*>(store[i].c_str()); }
  return wmain(argc, wargv);
}
