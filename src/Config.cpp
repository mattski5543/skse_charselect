// Loads CharSelect.ini at startup. The parsing itself is in core/ConfigParse.cpp.

#include "Config.h"

#include "core/Encoding.h"

namespace charselect::config
{
	namespace
	{
		Settings g_settings;
	}

	const Settings& Get()
	{
		return g_settings;
	}

	void Load()
	{
		const auto path = std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "CharSelect.ini";
		std::ifstream in(path);
		if (!in) {
			logger::info("Config: {} not found, using defaults", text::PathUtf8(path));
			return;
		}

		ParseLog log;
		Parse(in, g_settings, log);
		for (const auto& line : log.infos) {
			logger::info("{}", line);
		}
		for (const auto& line : log.warnings) {
			logger::warn("{}", line);
		}

		const auto& s = g_settings;
		logger::info("Config: replaceMainMenu={} hotkey={:#x} cell='{}' fallback='{}' max={}", s.replaceMainMenu, s.lobbyHotkey, s.lobbyCell,
			s.fallbackCell, s.maxCharacters);
	}
}
