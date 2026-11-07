// Finds the save folder (including Vortex and MO2 profile saves) and the
// paths of CharSelect's own files.

#include "Paths.h"

#include "SaveIndex.h"
#include "core/Encoding.h"

namespace charselect::paths
{
	std::optional<std::filesystem::path> MyGamesDir()
	{
		// log_directory() is <My Games>\Skyrim Special Edition[ GOG]\SKSE
		auto skseDir = logger::log_directory();
		if (!skseDir) {
			return std::nullopt;
		}
		return skseDir->parent_path();
	}

	std::optional<std::filesystem::path> SaveDir()
	{
		auto base = MyGamesDir();
		if (!base) {
			logger::error("SaveDir: could not resolve My Games folder");
			return std::nullopt;
		}

		std::string local = "Saves\\";
		if (auto* ini = RE::INISettingCollection::GetSingleton()) {
			if (auto* setting = ini->GetSetting("sLocalSavePath:General"); setting && setting->GetString()) {
				local = setting->GetString();
				logger::debug("SaveDir: sLocalSavePath = '{}'", local);
			} else {
				logger::warn("SaveDir: sLocalSavePath not found, defaulting to '{}'", local);
			}
		} else {
			logger::warn("SaveDir: INISettingCollection unavailable, defaulting to '{}'", local);
		}
		return *base / local;
	}

	std::optional<std::filesystem::path> SidecarPath(std::uint32_t a_characterId)
	{
		auto skseDir = logger::log_directory();
		if (!skseDir) {
			return std::nullopt;
		}
		return *skseDir / "CharSelect" / "Characters" / std::format("{}.json", FormatCharacterId(a_characterId));
	}

	std::string PresetName(std::uint32_t a_characterId)
	{
		return std::format("CharSelect_{}", FormatCharacterId(a_characterId));
	}

	std::filesystem::path PresetPath(std::uint32_t a_characterId)
	{
		// Skyrim's working directory is the game folder (MO2's VFS overlays it too).
		return std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "CharGen" / "Exported" /
		       std::format("{}.jslot", PresetName(a_characterId));
	}

	std::string ForLog(const std::filesystem::path& a_path)
	{
		const auto under = [&](const std::optional<std::filesystem::path>& a_base, std::string_view a_name) -> std::optional<std::string> {
			if (!a_base) {
				return std::nullopt;
			}
			const auto relative = a_path.lexically_relative(*a_base);
			if (relative.empty() || relative.native().starts_with(L"..")) {
				return std::nullopt;
			}
			return std::format("<{}>\\{}", a_name, text::PathUtf8(relative));
		};
		// The folder under My Games by name: "Skyrim Special Edition", or the
		// GOG one.
		if (const auto myGames = MyGamesDir()) {
			if (auto shown = under(myGames, "My Games\\" + text::PathUtf8(myGames->filename()))) {
				return *shown;
			}
		}
		std::error_code ec;
		const auto      game = std::filesystem::current_path(ec);
		if (auto shown = under(ec ? std::nullopt : std::optional(game), "Skyrim")) {
			return *shown;
		}
		return text::PathUtf8(a_path.filename());
	}
}
