// Undoing what Skyrim reverts when the lobby's game ends (see ResetGuard.h).

#include "ResetGuard.h"

#include "Util.h"
#include "core/Encoding.h"

namespace charselect::resetguard
{
	namespace
	{
		constexpr std::size_t kMaxListed = 25;  // log lines per section
		constexpr std::size_t kMaxExamples = 5;

		using SlotMask = REX::EnumSet<RE::BGSBipedObjectForm::BipedObjectSlot, std::uint32_t>;

		struct ArmorState
		{
			RE::TESObjectARMO*              armor{ nullptr };
			std::vector<RE::TESObjectARMA*> addons;
			std::uint32_t                   slots{ 0 };
		};

		struct SkinState
		{
			const RE::TESForm* owner{ nullptr };  // the NPC or race
			RE::BGSSkinForm*   skinForm{ nullptr };
			RE::TESObjectARMO* skin{ nullptr };
		};

		struct KeywordState
		{
			const RE::TESForm*        owner{ nullptr };
			RE::BGSKeywordForm*       form{ nullptr };
			std::vector<RE::BGSKeyword*> keywords;
		};

		struct NpcState
		{
			RE::TESNPC*                                    npc{ nullptr };
			std::vector<RE::SpellItem*>                    spells;
			std::vector<RE::TESLevSpell*>                  levSpells;
			std::vector<RE::TESShout*>                     shouts;
			std::vector<std::pair<RE::BGSPerk*, std::int8_t>> perks;
			std::vector<RE::BGSHeadPart*>                  headParts;
			RE::BGSColorForm*                              hairColor{ nullptr };    // null if not a plugin record
			RE::BGSTextureSet*                             faceDetails{ nullptr };
		};

		// Compared across the switch and logged, never written back.
		struct Counts
		{
			std::uint32_t        factions{ 0 };
			std::uint32_t        packages{ 0 };
			std::uint32_t        items{ 0 };
			const RE::TESForm*   outfit{ nullptr };
			const RE::TESForm*   hairColor{ nullptr };
			const RE::TESForm*   faceNPC{ nullptr };
			const RE::TESForm*   race{ nullptr };
			const RE::TESForm*   npcClass{ nullptr };
			const RE::TESForm*   combatStyle{ nullptr };
			const RE::TESForm*   voiceType{ nullptr };
			float                height{ 0.0f };
			float                weight{ 0.0f };
			bool                 faceData{ false };
			std::uint32_t        tintLayers{ 0 };
		};

		struct HeadPartState
		{
			const RE::BGSHeadPart* part{ nullptr };
			const RE::TESForm*     textureSet{ nullptr };
			std::string            model;
		};

		struct TextureSetState
		{
			const RE::BGSTextureSet*   set{ nullptr };
			std::array<std::string, 8> paths;
		};

		// Only plugin records, and only pointers to plugin records: those are
		// never freed, so they're safe to keep and write back.
		struct Snapshot
		{
			std::string                                     why;
			std::unordered_map<RE::FormID, ArmorState>      armors;
			std::unordered_map<RE::FormID, SkinState>       skins;
			std::unordered_map<RE::FormID, KeywordState>    keywords;
			std::unordered_map<RE::FormID, NpcState>        npcs;
			std::unordered_map<RE::FormID, std::pair<const RE::TESNPC*, Counts>> counts;
			std::unordered_map<RE::FormID, HeadPartState>   headParts;
			std::unordered_map<RE::FormID, TextureSetState> textureSets;
		};

		std::optional<Snapshot> g_capture;

		bool IsPluginForm(const RE::TESForm* a_form)
		{
			return a_form && !a_form->IsDynamicForm();
		}

		// The plugin records in a game array, in order (runtime ones dropped).
		template <class T>
		std::vector<T*> PluginForms(T* const* a_data, std::size_t a_count)
		{
			std::vector<T*> out;
			for (std::size_t i = 0; a_data && i < a_count; ++i) {
				if (IsPluginForm(a_data[i])) {
					out.push_back(a_data[i]);
				}
			}
			return out;
		}

		// What the revert removed from a list: entries of a_was missing now, but
		// only if everything there now was there before. Anything a mod added
		// or swapped since means it isn't a plain removal, so nothing is missing.
		template <class T>
		std::vector<T*> Missing(T* const* a_now, std::size_t a_count, const std::vector<T*>& a_was)
		{
			if (a_count >= a_was.size()) {
				return {};
			}
			for (std::size_t i = 0; i < a_count; ++i) {
				if (std::ranges::find(a_was, a_now[i]) == a_was.end()) {
					return {};
				}
			}
			std::vector<T*> out;
			for (auto* w : a_was) {
				if (std::find(a_now, a_now + a_count, w) == a_now + a_count) {
					out.push_back(w);
				}
			}
			return out;
		}

		std::uint32_t PackageCount(const RE::TESNPC* a_npc)
		{
			std::uint32_t n = 0;
			// BSSimpleList's const iteration doesn't compile.
			for ([[maybe_unused]] const auto* p : const_cast<RE::TESNPC*>(a_npc)->aiPackages.packages) {
				++n;
			}
			return n;
		}

		Counts CountsOf(const RE::TESNPC* a_npc)
		{
			return Counts{ a_npc->factions.size(), PackageCount(a_npc), a_npc->numContainerObjects, a_npc->defaultOutfit,
				a_npc->headRelatedData ? a_npc->headRelatedData->hairColor : nullptr, a_npc->faceNPC, a_npc->race, a_npc->npcClass,
				a_npc->combatStyle, a_npc->voiceType, a_npc->height, a_npc->weight, a_npc->faceData != nullptr,
				a_npc->tintLayers ? a_npc->tintLayers->size() : 0 };
		}

		std::string_view TypeName(const RE::TESForm* a_form)
		{
			switch (a_form->GetFormType()) {
			case RE::FormType::NPC:
				return "NPC"sv;
			case RE::FormType::Race:
				return "race"sv;
			case RE::FormType::Armor:
				return "armor"sv;
			case RE::FormType::Weapon:
				return "weapon"sv;
			case RE::FormType::HeadPart:
				return "head part"sv;
			case RE::FormType::TextureSet:
				return "texture set"sv;
			default:
				return "form"sv;
			}
		}

		// "0001A2B3 EditorID [Plugin.esp]"
		std::string Describe(const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "none";
			}
			std::string out = std::format("{:08X}", a_form->GetFormID());
			if (const char* edid = a_form->GetFormEditorID(); edid && *edid) {
				out += " " + text::ToUtf8(edid);
			} else if (const auto* named = a_form->As<RE::TESFullName>(); named && named->GetFullName() && *named->GetFullName()) {
				out += std::format(" '{}'", text::ToUtf8(named->GetFullName()));
			}
			if (const auto owner = OwningPlugin(a_form)) {
				out += std::format(" [{}]", text::ToUtf8(owner->file->GetFilename()));
			}
			return out;
		}

		std::string Join(const std::vector<std::string>& a_parts)
		{
			std::string out;
			for (const auto& p : a_parts) {
				out += (out.empty() ? "" : "; ") + p;
			}
			return out;
		}

		std::string Str(const char* a_text)
		{
			return a_text ? text::ToUtf8(a_text) : std::string{};
		}

		template <class F>
		void ForEachForm(F&& a_fn)
		{
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			const RE::BSReadLockGuard guard{ lock };
			if (!map) {
				return;
			}
			for (const auto& [id, form] : *map) {
				if (IsPluginForm(form)) {
					a_fn(id, form);
				}
			}
		}

		void TakeNpc(Snapshot& a_s, RE::FormID a_id, RE::TESNPC* a_npc)
		{
			NpcState st{ a_npc };
			if (const auto* d = a_npc->actorEffects) {
				st.spells = PluginForms(d->spells, d->numSpells);
				st.levSpells = PluginForms(d->levSpells, d->numlevSpells);
				st.shouts = PluginForms(d->shouts, d->numShouts);
			}
			for (std::uint32_t i = 0; a_npc->perks && i < a_npc->perkCount; ++i) {
				if (IsPluginForm(a_npc->perks[i].perk)) {
					st.perks.emplace_back(a_npc->perks[i].perk, a_npc->perks[i].currentRank);
				}
			}
			st.headParts = PluginForms(a_npc->headParts, a_npc->numHeadParts > 0 ? static_cast<std::size_t>(a_npc->numHeadParts) : 0);
			if (const auto* head = a_npc->headRelatedData) {
				st.hairColor = IsPluginForm(head->hairColor) ? head->hairColor : nullptr;
				st.faceDetails = IsPluginForm(head->faceDetails) ? head->faceDetails : nullptr;
			}
			a_s.npcs.emplace(a_id, std::move(st));
			a_s.counts.emplace(a_id, std::pair{ a_npc, CountsOf(a_npc) });
		}

		Snapshot Take(std::string_view a_why)
		{
			Snapshot s{ std::string(a_why) };
			ForEachForm([&](RE::FormID a_id, RE::TESForm* a_form) {
				switch (a_form->GetFormType()) {
				case RE::FormType::Armor:
					{
						auto* armor = static_cast<RE::TESObjectARMO*>(a_form);
						ArmorState st{ armor, { armor->armorAddons.begin(), armor->armorAddons.end() },
							armor->bipedModelData.bipedObjectSlots.underlying() };
						if (std::ranges::all_of(st.addons, IsPluginForm)) {
							s.armors.emplace(a_id, std::move(st));
						}
						break;
					}
				case RE::FormType::NPC:
					TakeNpc(s, a_id, static_cast<RE::TESNPC*>(a_form));
					break;
				case RE::FormType::HeadPart:
					{
						const auto* part = static_cast<const RE::BGSHeadPart*>(a_form);
						s.headParts.emplace(a_id, HeadPartState{ part, part->textureSet, Str(part->GetModel()) });
						break;
					}
				case RE::FormType::TextureSet:
					{
						const auto* set = static_cast<const RE::BGSTextureSet*>(a_form);
						TextureSetState st{ set };
						for (std::size_t i = 0; i < st.paths.size(); ++i) {
							st.paths[i] = Str(set->textures[i].textureName.c_str());
						}
						s.textureSets.emplace(a_id, std::move(st));
						break;
					}
				default:
					break;
				}
				if (auto* skinForm = a_form->As<RE::BGSSkinForm>(); skinForm && IsPluginForm(skinForm->skin) &&
																	(a_form->Is(RE::FormType::NPC) || a_form->Is(RE::FormType::Race))) {
					s.skins.emplace(a_id, SkinState{ a_form, skinForm, skinForm->skin });
				}
				if (a_form->Is(RE::FormType::NPC, RE::FormType::Race, RE::FormType::Armor, RE::FormType::Weapon)) {
					if (auto* kw = a_form->As<RE::BGSKeywordForm>()) {
						s.keywords.emplace(a_id, KeywordState{ a_form, kw, PluginForms(kw->keywords, kw->numKeywords) });
					}
				}
			});
			return s;
		}

		// One log line per kind of fix, with a few examples.
		struct Fixes
		{
			std::map<std::string, std::pair<std::size_t, std::vector<std::string>>> byKind;

			void Add(std::string a_kind, std::string a_example)
			{
				auto& [count, examples] = byKind[std::move(a_kind)];
				if (++count <= kMaxExamples) {
					examples.push_back(std::move(a_example));
				}
			}

			std::size_t Total() const
			{
				std::size_t n = 0;
				for (const auto& [kind, entry] : byKind) {
					n += entry.first;
				}
				return n;
			}
		};

		// Armor addons the revert removed (the armor holds only some of what it
		// held). Anything a mod has added or swapped since is left alone.
		void RestoreArmors(const Snapshot& a_s, Fixes& a_fixes)
		{
			for (const auto& [id, st] : a_s.armors) {
				auto&      now = st.armor->armorAddons;
				const auto missing = Missing(now.data(), now.size(), st.addons);
				if (missing.empty()) {
					continue;
				}
				const auto had = now.size();
				now.clear();
				for (auto* addon : st.addons) {
					now.push_back(addon);
				}
				st.armor->bipedModelData.bipedObjectSlots = SlotMask(static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(st.slots));
				a_fixes.Add("armor addons", std::format("{} {}->{}", Describe(st.armor), had, st.addons.size()));
			}
		}

		// NPC and race skins the revert emptied. A skin that changed to another
		// skin is left alone (a mod may have assigned it since).
		void RestoreSkins(const Snapshot& a_s, Fixes& a_fixes)
		{
			for (const auto& [id, st] : a_s.skins) {
				if (!st.skinForm->skin) {
					st.skinForm->skin = st.skin;
					a_fixes.Add("skins", std::format("{} on {}", Describe(st.skin), Describe(st.owner)));
				}
			}
		}

		void RestoreKeywords(const Snapshot& a_s, Fixes& a_fixes)
		{
			for (const auto& [id, st] : a_s.keywords) {
				const auto missing = Missing(st.form->keywords, st.form->numKeywords, st.keywords);
				if (!missing.empty()) {
					st.form->AddKeywords(missing);
					a_fixes.Add(std::format("{} keywords", TypeName(st.owner)), std::format("{} +{}", Describe(st.owner), missing.size()));
				}
			}
		}

		// Spells, perks and head parts of NPCs whose record the revert emptied.
		void RestoreNpcs(const Snapshot& a_s, Fixes& a_fixes)
		{
			for (const auto& [id, st] : a_s.npcs) {
				auto* npc = st.npc;
				auto* d = npc->actorEffects;
				const auto spells = d ? Missing(d->spells, d->numSpells, st.spells) : st.spells;
				const auto levSpells = d ? Missing(d->levSpells, d->numlevSpells, st.levSpells) : st.levSpells;
				const auto shouts = d ? Missing(d->shouts, d->numShouts, st.shouts) : st.shouts;
				if (!spells.empty() || !levSpells.empty() || !shouts.empty()) {
					if (!d) {
						npc->actorEffects = d = new RE::TESSpellList::SpellData();
					}
					if (!spells.empty()) {
						d->AddSpells(spells);
					}
					if (!levSpells.empty()) {
						d->AddLevSpells(levSpells);
					}
					if (!shouts.empty()) {
						d->AddShouts(shouts);
					}
					a_fixes.Add("NPC spells", std::format("{} +{}", Describe(npc), spells.size() + levSpells.size() + shouts.size()));
				}

				std::vector<RE::BGSPerk*> perksNow;
				for (std::uint32_t i = 0; npc->perks && i < npc->perkCount; ++i) {
					perksNow.push_back(npc->perks[i].perk);
				}
				std::vector<RE::BGSPerk*> perksWas;
				for (const auto& [perk, rank] : st.perks) {
					perksWas.push_back(perk);
				}
				if (const auto missing = Missing(perksNow.data(), perksNow.size(), perksWas); !missing.empty()) {
					for (const auto& [perk, rank] : st.perks) {
						if (std::ranges::find(missing, perk) != missing.end()) {
							npc->AddPerk(perk, rank);
						}
					}
					a_fixes.Add("NPC perks", std::format("{} +{}", Describe(npc), missing.size()));
				}

				const auto partsNow = npc->numHeadParts > 0 ? static_cast<std::size_t>(npc->numHeadParts) : 0;
				if (const auto missing = Missing(npc->headParts, partsNow, st.headParts); !missing.empty()) {
					auto* parts = RE::calloc<RE::BGSHeadPart*>(st.headParts.size());
					std::ranges::copy(st.headParts, parts);
					npc->headParts = parts;  // the old, shorter array is left as it is
					npc->numHeadParts = static_cast<std::int8_t>(st.headParts.size());
					a_fixes.Add("NPC head parts", std::format("{} {}->{}", Describe(npc), partsNow, st.headParts.size()));
				}

				// Hair color and face details went with the rest of the record:
				// Papyrus GetHairColor crashed on the emptied one (pink hair).
				if (st.hairColor || st.faceDetails) {
					auto* head = npc->headRelatedData;
					const bool hair = st.hairColor && !(head && head->hairColor);
					const bool face = st.faceDetails && !(head && head->faceDetails);
					if (hair || face) {
						if (!head) {
							npc->headRelatedData = head = new RE::TESNPC::HeadRelatedData();
							head->hairColor = nullptr;
							head->faceDetails = nullptr;
						}
						if (hair) {
							head->hairColor = st.hairColor;
						}
						if (face) {
							head->faceDetails = st.faceDetails;
						}
						a_fixes.Add("NPC hair color and face details", Describe(npc));
					}
				}
			}
		}

		// Everything compared but not written back: what the switch changed in
		// NPC factions, packages, inventories, outfits and looks, and in head
		// parts and texture sets.
		void ReportOtherChanges(const Snapshot& a_s, std::string_view a_when)
		{
			Fixes changes;  // same tallying, different meaning
			auto count = [&](const RE::TESForm* a_form, std::string_view a_what, std::uint32_t a_was, std::uint32_t a_now) {
				if (a_was != a_now) {
					changes.Add(std::format("{} {} {}", TypeName(a_form), a_what, a_now < a_was ? "fewer" : "more"),
						std::format("{} {}->{}", Describe(a_form), a_was, a_now));
				}
			};
			auto pointer = [&](const RE::TESForm* a_form, std::string_view a_what, const RE::TESForm* a_was, const RE::TESForm* a_now) {
				if (a_was != a_now) {
					changes.Add(std::format("{} {} changed", TypeName(a_form), a_what), Describe(a_form));
				}
			};
			for (const auto& [id, entry] : a_s.counts) {
				const auto& [npc, was] = entry;
				const auto now = CountsOf(npc);
				count(npc, "factions", was.factions, now.factions);
				count(npc, "packages", was.packages, now.packages);
				count(npc, "inventory entries", was.items, now.items);
				pointer(npc, "outfit", was.outfit, now.outfit);
				pointer(npc, "hair color", was.hairColor, now.hairColor);
				pointer(npc, "face template", was.faceNPC, now.faceNPC);
				pointer(npc, "race", was.race, now.race);
				pointer(npc, "class", was.npcClass, now.npcClass);
				pointer(npc, "combat style", was.combatStyle, now.combatStyle);
				pointer(npc, "voice type", was.voiceType, now.voiceType);
				if (was.height != now.height || was.weight != now.weight) {
					changes.Add("NPC height or weight changed", std::format("{} {:.2f}/{:.0f}->{:.2f}/{:.0f}", Describe(npc), was.height, was.weight, now.height, now.weight));
				}
				if (was.faceData != now.faceData) {
					changes.Add(std::format("NPC face data {}", now.faceData ? "appeared" : "gone"), Describe(npc));
				}
				count(npc, "tint layers", was.tintLayers, now.tintLayers);
			}
			for (const auto& [id, st] : a_s.npcs) {
				const auto* d = st.npc->actorEffects;
				count(st.npc, "spells", static_cast<std::uint32_t>(st.spells.size() + st.levSpells.size() + st.shouts.size()),
					d ? d->numSpells + d->numlevSpells + d->numShouts : 0);
				count(st.npc, "perks", static_cast<std::uint32_t>(st.perks.size()), st.npc->perkCount);
				count(st.npc, "head parts", static_cast<std::uint32_t>(st.headParts.size()),
					st.npc->numHeadParts > 0 ? static_cast<std::uint32_t>(st.npc->numHeadParts) : 0);
			}
			for (const auto& [id, st] : a_s.keywords) {
				count(st.owner, "keywords", static_cast<std::uint32_t>(st.keywords.size()), st.form->numKeywords);
			}
			for (const auto& [id, st] : a_s.headParts) {
				pointer(st.part, "texture set", st.textureSet, st.part->textureSet);
				if (st.model != Str(st.part->GetModel())) {
					changes.Add("head part model changed", std::format("{} '{}'->'{}'", Describe(st.part), st.model, Str(st.part->GetModel())));
				}
			}
			for (const auto& [id, st] : a_s.textureSets) {
				for (std::size_t i = 0; i < st.paths.size(); ++i) {
					const auto now = Str(st.set->textures[i].textureName.c_str());
					if (now != st.paths[i]) {
						changes.Add("texture set path changed", std::format("{} #{} '{}'->'{}'", Describe(st.set), i, st.paths[i], now));
					}
				}
			}
			if (changes.byKind.empty()) {
				logger::info("ResetGuard: {}: nothing else changed across the switch", a_when);
				return;
			}
			std::size_t shown = 0;
			for (const auto& [what, entry] : changes.byKind) {
				if (++shown > kMaxListed) {
					logger::info("ResetGuard: {}: ({} more kinds of change not listed)", a_when, changes.byKind.size() - kMaxListed);
					break;
				}
				logger::info("ResetGuard: {}: {} on {} records, e.g. {}", a_when, what, entry.first, Join(entry.second));
			}
		}
	}

	void Capture(std::string_view a_why)
	{
		if (g_capture) {
			logger::info("ResetGuard: leaving the lobby's game ({}); keeping the capture from '{}'", a_why, g_capture->why);
			return;
		}
		const auto start = std::chrono::steady_clock::now();
		g_capture = Take(a_why);
		logger::info("ResetGuard: leaving the lobby's game ({}): captured {} armors, {} skins, {} keyword lists, {} NPCs, {} head parts, {} texture sets ({:.1f} ms)",
			a_why, g_capture->armors.size(), g_capture->skins.size(), g_capture->keywords.size(), g_capture->npcs.size(), g_capture->headParts.size(),
			g_capture->textureSets.size(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
	}

	void Restore(std::string_view a_when, bool a_gameStarted)
	{
		if (!g_capture) {
			return;
		}
		// What the switch changed, before anything is put back.
		ReportOtherChanges(*g_capture, a_when);
		Fixes fixes;
		RestoreArmors(*g_capture, fixes);
		RestoreSkins(*g_capture, fixes);
		RestoreKeywords(*g_capture, fixes);
		RestoreNpcs(*g_capture, fixes);
		logger::info("ResetGuard: {} (left the lobby's game: {}): {} fixes", a_when, g_capture->why, fixes.Total());
		for (const auto& [kind, entry] : fixes.byKind) {
			logger::info("ResetGuard:   put back {} on {} records, e.g. {}", kind, entry.first, Join(entry.second));
		}
		if (a_gameStarted) {
			g_capture.reset();
		}
	}
}
