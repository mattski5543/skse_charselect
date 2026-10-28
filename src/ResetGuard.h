#pragma once

// Leaving the lobby's game (a load, or back to the main menu) makes Skyrim
// revert some records to their plugin data, which a normal load in the real
// game doesn't do (the lobby is a game started with coc from the main menu;
// a console coc then a load does the same). Mods that change records at
// runtime don't expect that within one session: one skin mod fills its
// own spare skins the first time it meets a skin and never again, so after
// the revert every man wearing one renders without hands; skins that startup
// distributors gave NPCs came back empty too.
//
// Capture() records what's there just before the lobby's game ends; Restore()
// puts back what the revert took, conservatively: list entries that went
// missing (armor addons, keywords, NPC spells, perks, head parts; never
// replacing or dropping any) and fields that went empty (skins, hair color).
// NPC records the revert reset to blank defaults (the characters standing in
// the lobby) get every captured field back. Plugin records only. Other
// changes across the switch are logged, not touched.

namespace charselect::resetguard
{
	// Main thread, just before the lobby's game is left. Keeps an earlier
	// capture that hasn't been restored yet (a failed load already reverted).
	void Capture(std::string_view a_why);

	// Main thread, once the switch is done: the main menu opened, a new game
	// started, or a load finished. With a_gameStarted the capture is used up.
	void Restore(std::string_view a_when, bool a_gameStarted);
}
