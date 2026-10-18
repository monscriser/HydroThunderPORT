// Exercises ht::steam::Export against a fake Steam userdata tree.
// usage: test_steam_export <game_root with hydrothunder + steam_art> <scratch dir>
#include <cstdio>
#include <fstream>
#include <iostream>
#include "../../app/src/ht_steam_shortcut.h"

namespace fs = std::filesystem;
using namespace ht::steam;

static int Count(const Node& n, const std::string& name) {
  int c = 0;
  for (const auto& s : n.children) {
    const Node* app = Find(s.children, "AppName");
    if (app && app->value == name) ++c;
  }
  return c;
}

int main(int argc, char** argv) {
  const fs::path root = argv[1];
  const fs::path config = fs::path(argv[2]) / "Steam/userdata/12345/config";
  fs::remove_all(argv[2]);
  fs::create_directories(config);
  // An existing shortcut from another program must survive untouched.
  {
    std::vector<Node> nodes{{0, "shortcuts", {}, {Node{0, "0", {}, {Str("AppName", "Outro jogo"), Str("Exe", "\"C:\\x.exe\"")}}}}};
    std::string bytes;
    WriteNodes(bytes, nodes);
    std::ofstream(config / "shortcuts.vdf", std::ios::binary) << bytes;
  }
  uint32_t id1 = Export(root, config, false);
  uint32_t id2 = Export(root, config, false);  // second run must replace, not duplicate
  std::ifstream in(config / "shortcuts.vdf", std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)), {});
  size_t pos = 0;
  auto nodes = ReadNodes(bytes, pos);
  const Node& sc = nodes.at(0);
  std::printf("appid %u (estavel: %s)\n", id1, id1 == id2 ? "sim" : "NAO");
  std::printf("atalhos Hydro Thunder: %d | outro jogo preservado: %d\n",
              Count(sc, "Hydro Thunder Hurricane"), Count(sc, "Outro jogo"));
  for (const auto& s : sc.children) {
    const Node* app = Find(s.children, "AppName");
    if (!app || app->value != "Hydro Thunder Hurricane") continue;
    for (const char* k : {"Exe", "StartDir", "icon", "LaunchOptions"})
      if (const Node* f = Find(s.children, k)) std::printf("  %s = %s\n", k, f->value.c_str());
  }
  std::printf("grid/:\n");
  for (const auto& e : fs::directory_iterator(config / "grid"))
    std::printf("  %-28s %8ju bytes\n", e.path().filename().string().c_str(),
                (uintmax_t)fs::file_size(e.path()));
  std::ifstream json(config / "grid" / (std::to_string(id1) + ".json"));
  std::cout << "json: " << std::string((std::istreambuf_iterator<char>(json)), {}) << "\n";
  return 0;
}
