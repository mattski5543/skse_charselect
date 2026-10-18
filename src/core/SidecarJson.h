#pragma once

// The per-character sidecar file (SKSE\CharSelect\Characters\<ID>.json) and
// when to re-export a character's RaceMenu preset, free of the game so they
// can be unit tested. Sidecar.cpp captures the player and writes the file.

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace charselect::sidecar
{
	struct FormRef
	{
		std::string   plugin;
		std::uint32_t localId{ 0 };
		std::string   name;  // for logs only
		std::string   hand;  // "left" or "right" for a weapon or spell in hand, else empty

		bool operator==(const FormRef&) const = default;
	};

	struct Sidecar
	{
		static constexpr int kVersion = 1;

		int                      version{ kVersion };
		std::uint32_t            characterId{ 0 };
		std::string              saveName;  // save the capture came from (no extension)
		std::int64_t             capturedAt{ 0 };  // unix seconds
		std::string              name;
		std::uint32_t            level{ 0 };
		FormRef                  race;
		bool                     female{ false };
		float                    weight{ 0.0f };
		float                    height{ 1.0f };
		std::string              preset;  // RaceMenu preset name, empty if never exported
		std::vector<FormRef>     worn;    // a weapon held in both hands is listed once per hand
		std::vector<FormRef>     spells;  // spells in hand (not powers or shouts)
		std::vector<std::string> plugins;  // load order at capture time

		bool operator==(const Sidecar&) const = default;
	};

	[[nodiscard]] nlohmann::json ToJson(const Sidecar& a_sidecar);
	// Missing fields keep their defaults. Throws (nlohmann::json::exception
	// or std::exception) on malformed values.
	[[nodiscard]] Sidecar FromJson(const nlohmann::json& a_json);

	// A character's preset is re-exported when its files are missing, when
	// the last export is older than kPresetExportInterval (the export renders
	// the face, so not on every quicksave), or after RaceMenu closed.
	constexpr auto kPresetExportInterval = std::chrono::minutes(5);
	[[nodiscard]] bool NeedsPresetExport(bool a_filesMissing, std::optional<std::chrono::steady_clock::time_point> a_lastExport,
		std::chrono::steady_clock::time_point a_now, bool a_raceMenuDirty);
}
