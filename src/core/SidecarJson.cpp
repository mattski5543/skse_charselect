// The sidecar's JSON format (see SidecarJson.h).

#include "core/SidecarJson.h"

#include "SaveIndex.h"

#include <format>

#include <nlohmann/json.hpp>

namespace charselect::sidecar
{
	namespace
	{
		using json = nlohmann::json;

		json ToJson(const FormRef& a_ref)
		{
			json j = { { "plugin", a_ref.plugin }, { "formId", std::format("{:08X}", a_ref.localId) }, { "name", a_ref.name } };
			if (!a_ref.hand.empty()) {
				j["hand"] = a_ref.hand;
			}
			return j;
		}

		FormRef FormRefFromJson(const json& a_j)
		{
			FormRef ref;
			ref.plugin = a_j.value("plugin", "");
			ref.localId = static_cast<std::uint32_t>(std::stoul(a_j.value("formId", "0"), nullptr, 16));
			ref.name = a_j.value("name", "");
			ref.hand = a_j.value("hand", "");
			return ref;
		}

		json ToJson(const std::vector<FormRef>& a_refs)
		{
			json out = json::array();
			for (const auto& r : a_refs) {
				out.push_back(ToJson(r));
			}
			return out;
		}
	}

	json ToJson(const Sidecar& a_s)
	{
		return {
			{ "version", a_s.version },
			{ "characterId", FormatCharacterId(a_s.characterId) },
			{ "saveName", a_s.saveName },
			{ "capturedAt", a_s.capturedAt },
			{ "name", a_s.name },
			{ "level", a_s.level },
			{ "race", ToJson(a_s.race) },
			{ "female", a_s.female },
			{ "weight", a_s.weight },
			{ "height", a_s.height },
			{ "preset", a_s.preset },
			{ "worn", ToJson(a_s.worn) },
			{ "spells", ToJson(a_s.spells) },
			{ "plugins", a_s.plugins },
		};
	}

	Sidecar FromJson(const json& a_j)
	{
		Sidecar s;
		s.version = a_j.value("version", 0);
		s.characterId = static_cast<std::uint32_t>(std::stoul(a_j.value("characterId", "0"), nullptr, 16));
		s.saveName = a_j.value("saveName", "");
		s.capturedAt = a_j.value("capturedAt", std::int64_t{ 0 });
		s.name = a_j.value("name", "");
		s.level = a_j.value("level", 0u);
		if (a_j.contains("race")) {
			s.race = FormRefFromJson(a_j["race"]);
		}
		s.female = a_j.value("female", false);
		s.weight = a_j.value("weight", 0.0f);
		s.height = a_j.value("height", 1.0f);
		s.preset = a_j.value("preset", "");
		for (const auto& w : a_j.value("worn", json::array())) {
			s.worn.push_back(FormRefFromJson(w));
		}
		for (const auto& sp : a_j.value("spells", json::array())) {
			s.spells.push_back(FormRefFromJson(sp));
		}
		s.plugins = a_j.value("plugins", std::vector<std::string>{});
		return s;
	}

	bool NeedsPresetExport(bool a_filesMissing, std::optional<std::chrono::steady_clock::time_point> a_lastExport,
		std::chrono::steady_clock::time_point a_now, bool a_raceMenuDirty)
	{
		const bool stale = !a_lastExport || a_now - *a_lastExport >= kPresetExportInterval;
		return a_filesMissing || stale || a_raceMenuDirty;
	}
}
