#pragma once

// Keeping a character's ID through RaceMenu (see CharacterId.h in src/).

#include <cstdint>
#include <optional>

namespace charselect::character
{
	// After the game handled RaceMenu's name being confirmed, which gives the
	// player a new character ID (so later saves become another character):
	// the ID to put back, if any. Only with the setting on, outside the
	// lobby, for a character known in this game (a_known: from the names of
	// the saves loaded and made since the last new game or main menu), when
	// this just changed the game's ID. a_before is the game's ID before the
	// change: the known character's, or 0 when the game hasn't set it yet
	// (after a load, until the first save, when it sets the loaded save's
	// ID). Any other ID isn't ours to replace. A new game's character
	// creation has no known character, so it keeps the new ID.
	[[nodiscard]] std::optional<std::uint32_t> IdToKeep(bool a_enabled, bool a_lobbyIdle, std::optional<std::uint32_t> a_known, std::uint32_t a_before, std::uint32_t a_after);
}
