#pragma once

// Where CharSelect finds the saves and keeps its own files.

namespace charselect::paths
{
	// Documents\My Games\Skyrim Special Edition (or the GOG variant).
	std::optional<std::filesystem::path> MyGamesDir();

	// The active save folder, honouring sLocalSavePath (Vortex/MO2 profile
	// saves put saves in a subfolder such as Saves\<profile ID>\).
	std::optional<std::filesystem::path> SaveDir();

	// Our own data: Documents\...\SKSE\CharSelect\Characters\<ID>.json
	std::optional<std::filesystem::path> SidecarPath(std::uint32_t a_characterId);

	// RaceMenu preset name and its file. CharGen.SaveCharacter always writes
	// Data\SKSE\Plugins\CharGen\Exported\<name>.jslot relative to the game folder.
	std::string           PresetName(std::uint32_t a_characterId);
	std::filesystem::path PresetPath(std::uint32_t a_characterId);
}
