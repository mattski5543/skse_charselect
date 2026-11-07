#pragma once

// Parsing for CharSelect.ini, free of the game so it can be unit tested.
// config::Load (Config.cpp) reads the file and logs what this reports.

#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>
#include <vector>

namespace charselect::config
{
	struct Settings
	{
		// [General]
		bool          replaceMainMenu{ true };  // show the select screen instead of the vanilla main menu
		bool          replaceAtStartup{ true };  // also on the main menu the game starts on (0: only coming back from a game)
		std::uint32_t lobbyHotkey{ 0x41 };      // F7: enter the lobby from the vanilla main menu
		bool          debugLog{ false };        // detailed CharSelect.log for reporting a problem

		// [Lobby]
		std::string lobbyCell{ "WhiterunBanneredMare" };
		std::string fallbackCell{ "qasmoke" };
		std::size_t maxCharacters{ 5 };
		// Worn by the hooded figure standing in for a character whose look
		// isn't captured yet ("0x<local>|<plugin>", equipped in order): the
		// black warlock hood, robes and boots. A standalone hood, because
		// mods change combined ones: WACCF takes the hood off Hooded Monk
		// Robes.
		std::vector<std::string> standInOutfit{ "0x000C5D10|Skyrim.esm", "0x000C5D11|Skyrim.esm", "0x000C5D12|Skyrim.esm" };
	};

	// What parsing found worth logging, ready to log as is.
	struct ParseLog
	{
		std::vector<std::string> warnings;
	};

	// Applies the INI's keys on top of a_settings. Sections and keys are
	// case-insensitive; ';' and '#' start a comment anywhere on a line.
	void Parse(std::istream& a_in, Settings& a_settings, ParseLog& a_log);
}
