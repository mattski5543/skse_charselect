// Offline test harness for the save indexer: runs the same code the plugin
// uses against a saves folder, without launching Skyrim.
//
//   savescan.exe "<saves dir>" [--all]

#include "SaveIndex.h"
#include "core/Encoding.h"

#include <cstdio>
#include <format>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::cerr << "usage: savescan <saves dir> [--all]\n";
		return 2;
	}
	const bool showAll = argc > 2 && std::string(argv[2]) == "--all";

	const auto result = charselect::ScanSaveDirectory(argv[1]);
	std::cout << std::format("Scanned {} .ess files in {:.1f} ms: {} characters, {} failures\n",
		result.filesSeen, result.elapsedMs, result.characters.size(), result.failures.size());

	for (const auto& c : result.characters) {
		const auto& h = c.Latest().header;
		std::cout << std::format("\nCharacter {} \"{}\" lvl {} {} {} - {} saves, last played {}\n",
			charselect::FormatCharacterId(c.characterId), h.playerName, h.playerLevel, h.raceEditorId,
			h.playerSex ? "female" : "male", c.saves.size(), charselect::FormatFileTime(h.fileTime));

		const std::size_t shown = showAll ? c.saves.size() : std::min<std::size_t>(c.saves.size(), 5);
		for (std::size_t i = 0; i < shown; ++i) {
			const auto& s = c.saves[i];
			std::cout << std::format("  [{}] {} | lvl {} | {} | {} | v{} comp{} shot {}x{}\n",
				i + 1, s.stem, s.header.playerLevel, s.header.playerLocation,
				charselect::FormatFileTime(s.header.fileTime), s.header.version,
				s.header.compressionType, s.header.shotWidth, s.header.shotHeight);
		}
		if (shown < c.saves.size()) {
			std::cout << std::format("  ... {} more\n", c.saves.size() - shown);
		}
	}

	for (const auto& f : result.failures) {
		std::cout << std::format("FAIL {}: {}\n", charselect::text::PathUtf8(f.path.filename()), f.reason);
	}
	return 0;
}
