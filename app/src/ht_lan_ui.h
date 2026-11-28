#pragma once
// LAN play: in-game name and join-by-IP screens.
//
// The game's menus are data (a node graph inside Base.apf) with no hook for new
// items, so these two screens are drawn over the game by HtLanOverlay. On the
// Multiplayer screens a hint offers them: hold Y for the player name, hold Back to
// join by IP. Holding (about a second) keeps the quick tap for the game, where Y
// and Back already mean Refresh, Change Track, Invite Friends or Recover.
//
// Input reaches the game through XamInputGetState (ht_lan_xam.cpp), which hands each
// pad to FilterPad below: while a screen is open the game sees an idle controller.
#include <cstdint>
#include <filesystem>
#include <memory>

namespace rex::ui {
class ImGuiDrawer;
class ImGuiDialog;
}  // namespace rex::ui

namespace ht::lan {

// Implemented in ht_lan_xam.cpp.
bool PlayEnabled();
// On a Multiplayer screen, not in an online lobby/race or a split-screen race.
bool InMultiplayerMenus();
// The Join Game list (after a search, until a join or Back).
bool OnJoinList();
void LeaveJoinList();
// Trusts `ip` (host order) even outside the LAN ranges, as a player typed it, and
// asks it for games right away.
void JoinAddress(uint32_t ip);

// Implemented in ht_lan_panel.cpp. `gamepad` is the guest X_INPUT_GAMEPAD (big-endian).
void FilterPad(uint32_t user, uint8_t* gamepad);
std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer,
                                                    std::filesystem::path config_path);

}  // namespace ht::lan
