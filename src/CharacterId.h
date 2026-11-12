#pragma once

// Keeping a character one character through RaceMenu. Each character's saves
// share a character ID (the hex part of a save's name). Confirming the name
// as RaceMenu closes gives the player a new one, so after changing their
// look during play their next saves become another character, in the game's
// Load menu and in the lobby. With bKeepCharacterAfterRaceMenu, the ID the
// character had is put back (core/CharacterId.h says when).

#include <string_view>

namespace charselect::character
{
	// Wraps the save manager's handler for RaceMenu's name being confirmed,
	// where the game makes the new ID. Call once, at DataLoaded.
	void InstallKeepIdHook();

	// The character in play, from the names of the saves loaded and made:
	// the save loading (PreLoadGame), whether it loaded (PostLoadGame), and
	// each save.
	void OnPreLoad(std::string_view a_saveName);
	void OnLoaded(bool a_success);
	void OnSaved(std::string_view a_saveName);

	// A new game or the main menu: no character known.
	void Forget();
}
