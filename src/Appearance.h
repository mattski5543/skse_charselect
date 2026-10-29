#pragma once

// A character's look at each step between their real game and the select
// screen, written to the debug log (bDebugLog) so a report shows where it
// stops matching, and the saved look's head parts for the lobby to check
// RaceMenu's work against. Reads; never changes anything.

namespace charselect::appearance
{
	// The head parts in a character's saved look (RaceMenu preset), found by
	// plugin name and ID in this load order, light plugins included. Parts
	// that can't be found are left out; empty if the file can't be read.
	std::vector<RE::BGSHeadPart*> SavedHeadParts(std::uint32_t a_characterId);

	// Why RaceMenu's preset loading would leave a_part off a_npc ("" if it
	// applies it): not flagged for the record's sex, or the record's race
	// isn't in its valid-race list.
	std::string WhySkipped(const RE::BGSHeadPart* a_part, const RE::TESNPC* a_npc);

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
