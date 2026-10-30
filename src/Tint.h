#pragma once

// A character's face tint texture (Data\Textures\CharGen\Exported\CharSelect_<ID>.dds),
// where RaceMenu looks for it with their preset. CharSelect exports it itself
// (ExportFromPlayer). LoadCharacterEx points the lobby face's tint slot at it,
// and a missing texture renders pink, so the lobby writes a flat skin-tone
// placeholder when there's no export yet.

namespace charselect::tint
{
	[[nodiscard]] std::filesystem::path Path(std::uint32_t a_characterId);

	// True if a real export exists (not missing, not the placeholder).
	[[nodiscard]] bool HasRealExport(std::uint32_t a_characterId);

	// Writes a small uniform DDS in the preset's skin tone if no tint exists.
	void EnsurePlaceholder(std::uint32_t a_characterId);

	// Our own export of the player's rendered face tint (skin tone, makeup,
	// war paint) to Path(): what RaceMenu's SaveCharacter is meant to write
	// but has been seen not to. Logs why if it can't.
	bool ExportFromPlayer(std::uint32_t a_characterId);
}
