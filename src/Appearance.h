#pragma once

// Logging only: a character's look at each step between their real game and
// the select screen, so a log shows where it stops matching (a mod hair and
// the face not matching in the lobby, 0.9.8). Reads; never changes anything.

namespace charselect::appearance
{
	// The record's head parts (and the player's overlay parts), hair colour,
	// face details, weight and face sliders.
	void LogRecord(std::string_view a_who, const RE::TESNPC* a_npc);

	// The character's saved look (RaceMenu preset): when it and its tint were
	// written, each head part as it resolves in this load order and whether
	// RaceMenu would apply it to a_npc's race and sex, then every difference
	// from a_npc (warnings). a_npc may be null: the saved look only.
	void CheckPreset(std::string_view a_who, std::uint32_t a_characterId, const RE::TESNPC* a_npc);

	// What the actor's head 3D holds: every object with its hair tint or face
	// tint texture, and the record's head parts that have no 3D.
	void Log3D(std::string_view a_who, RE::Actor* a_actor);
}
