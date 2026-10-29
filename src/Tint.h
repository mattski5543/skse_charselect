#pragma once

// The face tint texture RaceMenu exports with a preset
// (Data\Textures\CharGen\Exported\CharSelect_<ID>.dds). LoadCharacterEx points
// the lobby face's tint slot at it, and a missing texture renders pink, so the
// lobby writes a flat skin-tone placeholder when the real export is missing.

namespace charselect::tint
{
	[[nodiscard]] std::filesystem::path Path(std::uint32_t a_characterId);

	// True if a real RaceMenu export exists (not missing, not our placeholder).
	[[nodiscard]] bool HasRealExport(std::uint32_t a_characterId);

	// Writes a small uniform DDS in the preset's skin tone if no tint exists.
	void EnsurePlaceholder(std::uint32_t a_characterId);

	// Our own export of the player's rendered face tint (skin tone, makeup,
	// war paint) to Path(): what RaceMenu's SaveCharacter is meant to write
	// but never did on the user's setup. Logs why if it can't.
	bool ExportFromPlayer(std::uint32_t a_characterId);
}
