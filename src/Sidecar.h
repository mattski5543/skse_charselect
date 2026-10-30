#pragma once

// Per-character appearance sidecar, written on every save and after every load.
//
// One file per character (keyed by the character ID in the save name), not
// per save: the select screen only shows a character's latest look, and a
// RaceMenu preset per save would add ~1 MB of files per save. The JSON records
// which save it was captured from.
//
// Appearance itself lives in a RaceMenu preset (CharGen.SaveCharacter), which
// covers face, sculpt, tints, overlays, transforms and body morphs (OBody,
// BodySlide sliders), so any body mod works as long as it uses morphs.

#include "core/SidecarJson.h"

namespace charselect::sidecar
{
	// FormRef, Sidecar and the JSON format live in core/SidecarJson.h.

	std::optional<Sidecar> Read(std::uint32_t a_characterId);

	// SKSE SaveGame message handler. a_saveName has no extension.
	void OnSaveGame(std::string_view a_saveName);

	// Marks the preset dirty so the next save re-exports it immediately.
	void OnRaceMenuClosed();

	// Drops pending tint-export retries: after a load the player is someone
	// else, and a retry would save their look under the old character's ID.
	// Remembers which save is loading (a_saveName may end in .ess).
	void OnPreLoadGame(std::string_view a_saveName);

	// Back at the main menu (or a new game or the lobby next): the player is
	// nobody we know, so pending exports and a pending load capture are dropped.
	void OnMainMenuOpened();

	// Captures the loaded character as a save would, a moment after the load,
	// so characters show in the lobby without saving with the mod first.
	void OnPostLoadGame(bool a_success);
}
