// Logging a character's look at each step (see Appearance.h).

#include "Appearance.h"

#include "Paths.h"
#include "Tint.h"
#include "Util.h"
#include "core/Encoding.h"

#include <nlohmann/json.hpp>

namespace charselect::appearance
{
	namespace
	{
		using json = nlohmann::json;

		std::string PluginOf(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "-";
			}
			if (a_form->IsDynamicForm()) {
				return "runtime";
			}
			const auto owner = OwningPlugin(a_form);
			return owner ? text::ToUtf8(owner->file->GetFilename()) : "?";
		}

		std::string Describe(const RE::BGSHeadPart* a_part)
		{
			if (!a_part) {
				return "none";
			}
			return std::format("{:08X} {} type {} [{}]{}", a_part->GetFormID(), text::ToUtf8(a_part->formEditorID.c_str()),
				static_cast<int>(a_part->type.get()), PluginOf(a_part),
				a_part->flags.all(RE::BGSHeadPart::Flag::kIsExtraPart) ? " (extra part)" : "");
		}

		std::string Rgb(std::uint32_t a_rgb) { return std::format("#{:06X}", a_rgb & 0xFFFFFF); }

		std::optional<std::uint32_t> HairRgb(const RE::TESNPC* a_npc)
		{
			const auto* head = a_npc ? a_npc->headRelatedData : nullptr;
			if (!head || !head->hairColor) {
				return std::nullopt;
			}
			const auto& c = head->hairColor->color;
			return static_cast<std::uint32_t>(c.red) << 16 | static_cast<std::uint32_t>(c.green) << 8 | c.blue;
		}

		std::vector<const RE::BGSHeadPart*> Parts(const RE::TESNPC* a_npc)
		{
			std::vector<const RE::BGSHeadPart*> out;
			if (a_npc && a_npc->headParts) {
				for (std::int8_t i = 0; i < a_npc->numHeadParts; ++i) {
					out.push_back(a_npc->headParts[i]);
				}
			}
			return out;
		}

		std::string FileTime(const std::filesystem::path& a_path)
		{
			std::error_code ec;
			const auto      time = std::filesystem::last_write_time(a_path, ec);
			if (ec) {
				return "missing";
			}
			const auto utc = std::chrono::floor<std::chrono::seconds>(std::chrono::clock_cast<std::chrono::system_clock>(time));
			return std::format("written {:%Y-%m-%d %H:%M:%S} UTC", utc);
		}

		// A preset's "Plugin|local ID" (or an editor ID) resolved in this load
		// order, the way RaceMenu reads it: by plugin name, so a plugin that
		// moved is still found.
		struct Resolved
		{
			const RE::TESForm* form{ nullptr };
			std::string        problem;
		};

		Resolved Resolve(const std::string& a_identifier)
		{
			const auto bar = a_identifier.find('|');
			if (bar == std::string::npos) {
				auto* form = RE::TESForm::LookupByEditorID(a_identifier);
				return { form, form ? "" : "no form with that editor ID" };
			}
			const auto    plugin = a_identifier.substr(0, bar);
			std::uint32_t local = 0;
			try {
				local = static_cast<std::uint32_t>(std::stoul(a_identifier.substr(bar + 1), nullptr, 16));
			} catch (const std::exception&) {
				return { nullptr, "unreadable ID" };
			}
			auto*       dh = RE::TESDataHandler::GetSingleton();
			const auto* file = dh ? dh->LookupModByName(plugin) : nullptr;
			if (!file) {
				return { nullptr, "plugin not installed" };
			}
			if (file->compileIndex == 0xFF) {
				return { nullptr, "plugin not active" };
			}
			// RaceMenu writes the low 24 bits even for a light plugin (its slot
			// included); only the low 12 are the plugin's own.
			local &= file->IsLight() ? 0xFFFu : 0xFFFFFFu;
			auto* form = dh->LookupForm(local, plugin);
			return { form, form ? "" : "no such form in the plugin" };
		}

		// Why RaceMenu's ApplyPresetData would skip a part on this record:
		// it applies only parts flagged for the record's sex whose valid-race
		// list holds the record's race.
		std::string WouldSkip(const RE::BGSHeadPart* a_part, const RE::TESNPC* a_npc)
		{
			if (!a_npc) {
				return "";
			}
			const bool female = a_npc->actorData.actorBaseFlags.all(RE::ACTOR_BASE_DATA::Flag::kFemale);
			if (!a_part->flags.all(female ? RE::BGSHeadPart::Flag::kFemale : RE::BGSHeadPart::Flag::kMale)) {
				return std::format("not for {}", female ? "women" : "men");
			}
			if (!a_part->validRaces) {
				return "no valid-race list";
			}
			if (!a_npc->race || !a_part->validRaces->HasForm(a_npc->race)) {
				return std::format("race {} not in its list {:08X}", a_npc->race ? a_npc->race->formEditorID.c_str() : "none",
					a_part->validRaces->GetFormID());
			}
			return "";
		}
	}

	void LogRecord(std::string_view a_who, const RE::TESNPC* a_npc)
	{
		if (!a_npc) {
			logger::info("Look: {}: no record", a_who);
			return;
		}
		auto*      npc = const_cast<RE::TESNPC*>(a_npc);  // HasOverlays() isn't const
		const bool female = npc->actorData.actorBaseFlags.all(RE::ACTOR_BASE_DATA::Flag::kFemale);
		logger::info("Look: {}: record {:08X} race {:08X} {} {} weight {:.0f}, {} head parts", a_who, npc->GetFormID(),
			npc->race ? npc->race->GetFormID() : 0, npc->race ? npc->race->formEditorID.c_str() : "none", female ? "female" : "male",
			npc->weight, npc->numHeadParts);
		const auto parts = Parts(npc);
		for (std::size_t i = 0; i < parts.size(); ++i) {
			logger::info("Look:   part {}: {}", i, Describe(parts[i]));
		}
		if (npc->HasOverlays()) {
			// The player's parts as RaceMenu exports them, when the engine keeps them apart.
			auto* overlays = npc->GetBaseOverlays();
			const auto count = npc->GetNumBaseOverlays();
			for (std::uint32_t i = 0; overlays && i < count; ++i) {
				logger::info("Look:   overlay part {}: {}", i, Describe(overlays[i]));
			}
		}
		const auto* head = npc->headRelatedData;
		const auto  hair = HairRgb(npc);
		logger::info("Look:   hair colour {} {}, face details {:08X} [{}]", head && head->hairColor ? std::format("{:08X}", head->hairColor->GetFormID()) : "none",
			hair ? Rgb(*hair) : "-", head && head->faceDetails ? head->faceDetails->GetFormID() : 0, PluginOf(head ? head->faceDetails : nullptr));
		if (const auto* face = npc->faceData) {
			std::string morphs;
			for (const float m : face->morphs) {
				morphs += std::format("{}{:.2f}", morphs.empty() ? "" : " ", m);
			}
			logger::info("Look:   face sliders [{}] presets [{} {} {} {}]", morphs, face->parts[0], face->parts[1], face->parts[2], face->parts[3]);
		} else {
			logger::info("Look:   no face data");
		}
	}

	void CheckPreset(std::string_view a_who, std::uint32_t a_characterId, const RE::TESNPC* a_npc)
	{
		const auto path = paths::PresetPath(a_characterId);
		logger::info("Look: {}: saved look {} {}, tint {}", a_who, text::PathUtf8(path.filename()), FileTime(path), FileTime(tint::Path(a_characterId)));
		json root;
		try {
			std::ifstream in(path, std::ios::binary);
			if (!in) {
				return;
			}
			root = json::parse(in);
		} catch (const std::exception& e) {
			logger::warn("Look: {}: saved look unreadable: {}", a_who, e.what());
			return;
		}

		std::vector<const RE::BGSHeadPart*> saved;
		if (const auto it = root.find("headParts"); it != root.end() && it->is_array()) {
			for (const auto& entry : *it) {
				const auto identifier = entry.value("formIdentifier", std::string{});
				const auto stored = entry.value("formId", std::uint32_t{ 0 });
				const auto resolved = Resolve(identifier);
				const auto* part = resolved.form ? resolved.form->As<RE::BGSHeadPart>() : nullptr;
				if (!part) {
					logger::warn("Look:   saved part {} (stored as {:08X}): {}", text::ToUtf8(identifier), stored,
						resolved.form ? "not a head part" : resolved.problem);
					continue;
				}
				saved.push_back(part);
				const auto skip = WouldSkip(part, a_npc);
				logger::info("Look:   saved part {} -> {}{}{}", text::ToUtf8(identifier), Describe(part),
					part->GetFormID() != stored ? std::format(" (stored as {:08X}: the plugin moved in the load order)", stored) : "",
					skip.empty() ? "" : std::format("; RaceMenu would skip it: {}", skip));
			}
		}
		const auto& actor = root.contains("actor") && root["actor"].is_object() ? root["actor"] : json::object();
		const auto  savedHair = actor.contains("hairColor") ? std::optional(actor["hairColor"].get<std::uint32_t>()) : std::nullopt;
		const auto  headTexture = actor.value("headTexture", std::string{});
		const auto  faceDetails = headTexture.empty() ? Resolved{} : Resolve(headTexture);
		logger::info("Look:   saved hair colour {}, face details {} -> {}, weight {:.0f}", savedHair ? Rgb(*savedHair) : "-",
			headTexture.empty() ? "-" : text::ToUtf8(headTexture),
			faceDetails.form ? std::format("{:08X}", faceDetails.form->GetFormID()) : faceDetails.problem.empty() ? "-" : faceDetails.problem,
			actor.value("weight", -1.0f));

		std::vector<float> sliders;
		std::vector<int>   presets;
		try {
			const auto& def = root.at("morphs").at("default");
			for (const auto& v : def.at("morphs")) {
				sliders.push_back(v.get<float>());
			}
			for (const auto& v : def.at("presets")) {
				presets.push_back(static_cast<int>(v.get<std::int64_t>()));
			}
		} catch (const std::exception&) {
		}

		if (!a_npc) {
			return;
		}
		// Every way the record now differs from the saved look.
		std::size_t differences = 0;
		const auto  note = [&](const std::string& a_what) {
			++differences;
			logger::warn("Look: {}: differs from the saved look: {}", a_who, a_what);
		};
		const auto parts = Parts(a_npc);
		for (const auto* part : saved) {
			if (std::ranges::find(parts, part) == parts.end()) {
				note(std::format("saved part {} is not on the record", Describe(part)));
			}
		}
		for (const auto* part : parts) {
			if (part && !part->flags.all(RE::BGSHeadPart::Flag::kIsExtraPart) && std::ranges::find(saved, part) == saved.end()) {
				note(std::format("record has {}, not in the saved look", Describe(part)));
			}
		}
		const auto hair = HairRgb(a_npc);
		if (savedHair && hair != (*savedHair & 0xFFFFFF)) {
			note(std::format("hair colour {} (saved {})", hair ? Rgb(*hair) : "none", Rgb(*savedHair)));
		}
		const auto* head = a_npc->headRelatedData;
		if (faceDetails.form && (!head || head->faceDetails != faceDetails.form)) {
			note(std::format("face details {:08X} (saved {:08X})", head && head->faceDetails ? head->faceDetails->GetFormID() : 0,
				faceDetails.form->GetFormID()));
		}
		if (const auto* face = a_npc->faceData) {
			std::string changed;
			for (std::size_t i = 0; i < sliders.size() && i < std::size(face->morphs); ++i) {
				if (std::abs(sliders[i]) < 1e30f && std::abs(sliders[i] - face->morphs[i]) > 0.001f) {
					changed += std::format(" {}: {:.2f} (saved {:.2f})", i, face->morphs[i], sliders[i]);
				}
			}
			for (std::size_t i = 0; i < presets.size() && i < std::size(face->parts); ++i) {
				if (presets[i] != face->parts[i]) {
					changed += std::format(" preset {}: {} (saved {})", i, face->parts[i], presets[i]);
				}
			}
			if (!changed.empty()) {
				note("face sliders" + changed);
			}
		}
		if (differences == 0) {
			logger::info("Look: {}: matches the saved look (head parts, hair colour, face details, face sliders)", a_who);
		}
	}

	void Log3D(std::string_view a_who, RE::Actor* a_actor)
	{
		auto* face = a_actor ? a_actor->GetFaceNodeSkinned() : nullptr;
		if (!face) {
			logger::info("Look: {}: no head 3D (3D loaded: {})", a_who, a_actor && a_actor->Is3DLoaded());
			return;
		}
		auto& children = face->GetChildren();
		logger::info("Look: {}: head 3D has {} objects", a_who, children.size());
		for (auto& child : children) {
			if (!child) {
				continue;
			}
			std::string what = child->AsNode() ? "node" : "object";
			if (auto* geometry = child->AsGeometry()) {
				auto* shader = geometry->lightingShaderProp_cast();
				auto* material = shader ? shader->material : nullptr;
				if (!material) {
					what = "geometry, no lighting material";
				} else if (material->GetFeature() == RE::BSShaderMaterial::Feature::kHairTint) {
					// Tint values run 0-2 (RaceMenu writes colour / 255 * 2).
					const auto& c = static_cast<RE::BSLightingShaderMaterialHairTint*>(material)->tintColor;
					const auto  byte = [](float a_v) { return static_cast<std::uint32_t>(std::clamp(a_v * 127.5f, 0.0f, 255.0f)); };
					what = std::format("hair tint ({:.2f}, {:.2f}, {:.2f}) about {}", c.red, c.green, c.blue,
						Rgb(byte(c.red) << 16 | byte(c.green) << 8 | byte(c.blue)));
				} else if (material->GetFeature() == RE::BSShaderMaterial::Feature::kFaceGen) {
					const auto& tint = static_cast<RE::BSLightingShaderMaterialFacegen*>(material)->tintTexture;
					what = std::format("face, tint texture '{}'", tint ? text::ToUtf8(tint->name.c_str()) : "none");
				} else {
					what = std::format("geometry, material feature {}", static_cast<int>(material->GetFeature()));
				}
			}
			logger::info("Look:   3D '{}': {}", text::ToUtf8(child->name.c_str()), what);
		}
		std::string missing;
		for (const auto* part : Parts(a_actor->GetActorBase())) {
			if (part && !face->GetObjectByName(part->formEditorID)) {
				missing += std::format("{}{}", missing.empty() ? "" : ", ", Describe(part));
			}
		}
		if (!missing.empty()) {
			logger::warn("Look: {}: head parts with no 3D: {}", a_who, missing);
		}
	}
}
