#include "ht_setup.h"
#include "ht_content_install.h"
#include <rex/logging.h>
#include <cstdio>
int main(int argc, char** argv) {
  rex::InitLoggingEarly();
  if (argc == 5) {
    try {
      ht::PrepareContent(argv[1], {argv[2],argv[3],argv[4]}, [](std::string s) {std::printf("%s\n",s.c_str());});
      return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
  }
  if (argc != 3) {
    std::fprintf(stderr, "Usage: ht-import package destination\n");
    return 2;
  }
  return ht::ExtractPackageTo(argv[1], argv[2]) ? 0 : 1;
}
