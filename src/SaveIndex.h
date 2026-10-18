#pragma once

// Save file indexing. Deliberately free of CommonLib so the offline
// savescan tool and the unit tests can share it: std and core only, no logging.

#include "core/Encoding.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace charselect
{
	// Fields of the .ess header (UESP "Save File Format", SSE version 12).
	// Strings are converted from the game's code page to UTF-8.
	struct SaveHeader
	{
		std::uint32_t version{ 0 };
		std::uint32_t saveNumber{ 0 };
		std::string   playerName;
		std::uint32_t playerLevel{ 0 };
		std::string   playerLocation;
		std::string   gameDate;
		std::string   raceEditorId;
		std::uint16_t playerSex{ 0 };  // 0 male, 1 female
		float         playerCurExp{ 0.0f };
		float         playerLvlUpExp{ 0.0f };
		std::uint64_t fileTime{ 0 };  // FILETIME layout, local time, 100ns ticks since 1601
		std::uint32_t shotWidth{ 0 };
		std::uint32_t shotHeight{ 0 };
		std::uint16_t compressionType{ 0 };  // 0 none, 1 zlib, 2 lz4
	};

	// Parts of a game-generated save name, e.g.
	// Save7_1A2B3C4D_1_5376656E_Tamriel_071559_20261013204815_66_1
	struct SaveNameParts
	{
		std::string   saveType;     // "Save", "Autosave", "Quicksave"
		std::uint32_t slot{ 0 };     // the number after the type
		std::uint32_t characterId{ 0 };
	};

	struct SaveFile
	{
		std::filesystem::path path;
		std::string           stem;  // filename without extension
		SaveNameParts         name;
		SaveHeader            header;
	};

	struct Character
	{
		std::uint32_t         characterId{ 0 };
		std::vector<SaveFile> saves;  // newest first

		[[nodiscard]] const SaveFile& Latest() const { return saves.front(); }
	};

	struct ScanFailure
	{
		std::filesystem::path path;
		std::string           reason;
	};

	struct ScanResult
	{
		std::vector<Character>   characters;  // most recently played first
		std::vector<ScanFailure> failures;
		std::size_t              filesSeen{ 0 };
		double                   elapsedMs{ 0.0 };
	};

	// Reads only the header bytes, not the whole save.
	// a_codePage: the code page the game wrote the strings in (the system's,
	// unless a test says otherwise).
	std::optional<SaveHeader> ReadSaveHeader(const std::filesystem::path& a_path, std::string& a_error, unsigned a_codePage = text::kGameCodePage);

	// Returns nullopt for names that don't follow the game's pattern (e.g. a
	// console "save foo" name), since those carry no character ID.
	std::optional<SaveNameParts> ParseSaveName(std::string_view a_stem);

	ScanResult ScanSaveDirectory(const std::filesystem::path& a_dir, unsigned a_codePage = text::kGameCodePage);

	std::string FormatCharacterId(std::uint32_t a_id);           // "1A2B3C4D"
	// The game stores local wall-clock time in the header FILETIME (it matches
	// the timestamp in the filename), so this formats it without a zone shift.
	std::string FormatFileTime(std::uint64_t a_fileTime);  // "2026-10-13 20:48:15"
	std::int64_t FileTimeToUnixSeconds(std::uint64_t a_fileTime);
}
