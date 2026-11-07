// Parsing CharSelect.ini (see ConfigParse.h).

#include "core/ConfigParse.h"

#include "core/Text.h"

#include <algorithm>
#include <exception>
#include <format>
#include <utility>

namespace charselect::config
{
	namespace
	{
		bool ParseBool(const std::string& a_value)
		{
			const auto v = text::Lower(a_value);
			return v == "1" || v == "true" || v == "yes" || v == "on";
		}
	}

	void Parse(std::istream& a_in, Settings& a_settings, ParseLog& a_log)
	{
		std::string section;
		std::string line;
		bool        first = true;
		while (std::getline(a_in, line)) {
			// Editors may save a UTF-8 byte order mark.
			if (std::exchange(first, false) && line.starts_with("\xEF\xBB\xBF")) {
				line.erase(0, 3);
			}
			if (const auto comment = line.find_first_of(";#"); comment != std::string::npos) {
				line.erase(comment);
			}
			line = text::Trim(line);
			if (line.empty()) {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = text::Lower(text::Trim(std::string_view(line).substr(1, line.size() - 2)));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = text::Lower(text::Trim(std::string_view(line).substr(0, eq)));
			const auto value = text::Trim(std::string_view(line).substr(eq + 1));
			const auto id = section + "." + key;

			try {
				if (id == "general.breplacemainmenu") {
					a_settings.replaceMainMenu = ParseBool(value);
				} else if (id == "general.breplaceatstartup") {
					a_settings.replaceAtStartup = ParseBool(value);
				} else if (id == "general.blobbyonload") {
					a_settings.lobbyOnLoad = ParseBool(value);
				} else if (id == "general.ilobbyhotkey") {
					// Hex with 0x, decimal otherwise (not octal for a leading 0).
					const bool hex = value.starts_with("0x") || value.starts_with("0X");
					a_settings.lobbyHotkey = static_cast<std::uint32_t>(std::stoul(value, nullptr, hex ? 16 : 10));
				} else if (id == "general.bdebuglog") {
					a_settings.debugLog = ParseBool(value);
				} else if (id == "lobby.slobbycell") {
					a_settings.lobbyCell = value;
				} else if (id == "lobby.sfallbackcell") {
					a_settings.fallbackCell = value;
				} else if (id == "lobby.imaxcharacters") {
					a_settings.maxCharacters = static_cast<std::size_t>(std::clamp(std::stol(value), 1L, 9L));  // stoul would wrap a negative
				} else if (id == "lobby.sstandinoutfit") {
					a_settings.standInOutfit.clear();
					std::string_view rest = value;
					while (!rest.empty()) {
						const auto comma = rest.find(',');
						if (auto item = text::Trim(rest.substr(0, comma)); !item.empty()) {
							a_settings.standInOutfit.push_back(std::move(item));
						}
						rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
					}
				} else {
					a_log.warnings.push_back(std::format("Config: unknown key [{}] {}", section, key));
				}
			} catch (const std::exception& e) {
				a_log.warnings.push_back(std::format("Config: bad value for [{}] {} = '{}': {}", section, key, value, e.what()));
			}
		}
	}
}
