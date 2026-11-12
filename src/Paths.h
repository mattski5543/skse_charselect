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

	// RaceMenu preset name and its file, where CharGen.LoadCharacterEx reads
	// it: Data\SKSE\Plugins\CharGen\Exported\<name>.jslot, relative to the game
	// folder. CharGen.SaveCharacterPreset writes it to RaceMenu's Presets
	// folder instead (PresetStagingPath), and it's moved across from there.
	inline constexpr std::string_view kPresetPrefix = "CharSelect_";
	std::string           PresetName(std::uint32_t a_characterId);
	std::filesystem::path PresetDir();
	std::filesystem::path PresetPath(std::uint32_t a_characterId);
	std::filesystem::path PresetStagingDir();
	std::filesystem::path PresetStagingPath(std::uint32_t a_characterId);

	// A path for the log, UTF-8, without the Windows user name: under My
	// Games it starts "<My Games\Skyrim Special Edition>\", under the game
	// folder "<Skyrim>\".
	// Players paste their logs in public.
	std::string ForLog(const std::filesystem::path& a_path);
}
