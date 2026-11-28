// The setup step for RecompLauncher, with no window:
//
//   hydrothunder --prepare --game=<package or folder> --update=<title update> [--dlc=<DLC>]
//
// It prepares the player's copy into <RECOMPLAUNCHER_DATA>/library with the same importer as
// the launcher's "Prepare files" button (PrepareContent), prints "progress <0-1> <text>" lines
// and exits. The game started with RECOMPLAUNCHER_DATA plays from that library.
//
// - The game and update are prepared in <data>/prepare.partial and swapped in whole, so
//   library/game_data is always a complete install (an interrupted swap is undone on the next
//   run). The DLC is unpacked beside it first in the same way.
// - <data>/library/hydrothunder-prepared.txt is written last and says which files were used.
//   Run again with the same files, or without the game (it is kept in the library), it prints
//   "progress 1 Already set up" and exits.
// - An OS lock on <data>/hydrothunder-prepare.lock stops two runs at once; a run that was
//   killed leaves no lock behind.
#pragma once

#include <filesystem>

namespace ht::prepare {
int Run(const std::filesystem::path& game, const std::filesystem::path& update,
        const std::filesystem::path& dlc);
}
