// htrecomp - first-run game data import.
//
// This project ships no game content. On first launch the player points us at
// their own copy, and we unpack it into the layout the runtime expects.
//
// Hydro Thunder Hurricane was a digital title, so a copy is an STFS package -
// the same container format used for its DLC. That means one importer handles
// both, which is why this file covers the optional DLC as well as the game.
//
// Where things land, and why:
//
//   the game  ->  <exe>/game_data/
//                 The runtime resolves game files from game_data_root, and
//                 refuses to start without game_data/default.xex.
//
//   the DLC   ->  <exe>/user_data/0000000000000000/5841096A/00000002/<name>/
//                 Content lives at content_root/xuid/title_id/type/name/, and
//                 ContentManager::ResolvePackagePath forces the xuid to ZERO
//                 for marketplace content ("DLCs are stored in common
//                 directory"). Installing it under the console's own id looks
//                 right and is silently never found.
//                 No .header file is needed: ContentManager::ListContent falls
//                 back to synthesising the metadata from the directory name.

#pragma once

#include <filesystem>

namespace ht {

// Makes sure game data is present, importing it from a package the player
// selects if it is not. Returns false only if the game genuinely cannot start:
// the player cancelled, or the import failed. Shows its own error messages.
//
// Safe to call every launch - it does nothing once game_data/default.xex is
// there.
bool EnsureGameData(const std::filesystem::path& exe_dir);
bool ExtractPackageTo(const std::filesystem::path& package, const std::filesystem::path& dest);

}  // namespace ht
