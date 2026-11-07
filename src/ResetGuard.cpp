// Undoing what Skyrim reverts when the lobby's game ends (see ResetGuard.h).

#include "ResetGuard.h"

#include "Util.h"
#include "core/Encoding.h"

#include <unordered_set>

namespace charselect::resetguard
{
	namespace
	{
		constexpr std::size_t kMaxListed = 25;  // log lines per section
		constexpr std::size_t kMaxExamples = 5;
		constexpr RE::FormID  kPlayerBaseId = 0x7;

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

			// The rest of the record, used only if it comes back emptied (see
			// FindEmptied). Values, not the record's own heap blocks: those are
			// freed when the record is emptied.
			RE::TESNPC*                                    faceNPC{ nullptr };
			float                                          height{ 1.0f };
			float                                          weight{ 0.0f };
			RE::TESClass*                                  npcClass{ nullptr };
			RE::TESCombatStyle*                            combatStyle{ nullptr };
			RE::BGSVoiceType*                              voiceType{ nullptr };
			RE::BGSOutfit*                                 defaultOutfit{ nullptr };
			RE::BGSOutfit*                                 sleepOutfit{ nullptr };
			std::vector<std::pair<RE::TESFaction*, std::int8_t>> factions;
			std::vector<RE::TESPackage*>                   packages;
			std::vector<std::pair<RE::TESBoundObject*, std::int32_t>> items;
			std::vector<RE::TESNPC::Layer>                 tints;
			std::optional<RE::TESNPC::FaceData>            faceData;
			bool                                           hadHead{ false };
		};

		// Compared across the switch and logged (debug), never written back.
		// Only the record's own fields: its head data and tint list may be
		// freed blocks on an emptied record, so nothing is read through them.
		struct Counts
		{
			std::uint32_t        factions{ 0 };
			std::uint32_t        packages{ 0 };
			std::uint32_t        items{ 0 };
			const RE::TESForm*   outfit{ nullptr };
			const RE::TESForm*   faceNPC{ nullptr };
			const RE::TESForm*   race{ nullptr };
			const RE::TESForm*   npcClass{ nullptr };
			const RE::TESForm*   combatStyle{ nullptr };
			const RE::TESForm*   voiceType{ nullptr };
			float                height{ 0.0f };
			float                weight{ 0.0f };
			bool                 faceData{ false };
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

		template <class T>
		T* PluginOrNull(T* a_form)
		{
			return IsPluginForm(a_form) ? a_form : nullptr;
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
			return Counts{ a_npc->factions.size(), PackageCount(a_npc), a_npc->numContainerObjects, a_npc->defaultOutfit, a_npc->faceNPC,
				a_npc->race, a_npc->npcClass, a_npc->combatStyle, a_npc->voiceType, a_npc->height, a_npc->weight, a_npc->faceData != nullptr };
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

		void TakeNpc(Snapshot& a_s, RE::FormID a_id, RE::TESNPC* a_npc, bool a_details)
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
				st.hairColor = PluginOrNull(head->hairColor);
				st.faceDetails = PluginOrNull(head->faceDetails);
			}
			st.hadHead = a_npc->headRelatedData != nullptr;
			st.faceNPC = PluginOrNull(a_npc->faceNPC);
			st.height = a_npc->height;
			st.weight = a_npc->weight;
			st.npcClass = PluginOrNull(a_npc->npcClass);
			st.combatStyle = PluginOrNull(a_npc->combatStyle);
			st.voiceType = PluginOrNull(a_npc->voiceType);
			st.defaultOutfit = PluginOrNull(a_npc->defaultOutfit);
			st.sleepOutfit = PluginOrNull(a_npc->sleepOutfit);
			for (const auto& f : a_npc->factions) {
				if (IsPluginForm(f.faction)) {
					st.factions.emplace_back(f.faction, f.rank);
				}
			}
			for (auto* package : a_npc->aiPackages.packages) {
				if (IsPluginForm(package)) {
					st.packages.push_back(package);
				}
			}
			for (std::uint32_t i = 0; a_npc->containerObjects && i < a_npc->numContainerObjects; ++i) {
				if (const auto* entry = a_npc->containerObjects[i]; entry && IsPluginForm(entry->obj)) {
					st.items.emplace_back(entry->obj, entry->count);
				}
			}
			if (a_npc->tintLayers) {
				for (const auto* layer : *a_npc->tintLayers) {
					if (layer) {
						st.tints.push_back(*layer);
					}
				}
			}
			if (a_npc->faceData) {
				st.faceData = *a_npc->faceData;
			}
			a_s.npcs.emplace(a_id, std::move(st));
			if (a_details) {
				a_s.counts.emplace(a_id, std::pair{ a_npc, CountsOf(a_npc) });
			}
		}

		// a_details: also what ReportOtherChanges compares (debug log only).
		Snapshot Take(std::string_view a_why, bool a_details)
		{
			Snapshot s{ std::string(a_why) };
			ForEachForm([&](RE::FormID a_id, RE::TESForm* a_form) {
				// Not the player's record: after a load it's the loaded character's,
				// set up by the save, and the lobby's player was someone else.
				if (a_id == kPlayerBaseId) {
					return;
				}
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
					TakeNpc(s, a_id, static_cast<RE::TESNPC*>(a_form), a_details);
					break;
				case RE::FormType::HeadPart:
					if (a_details) {
						const auto* part = static_cast<const RE::BGSHeadPart*>(a_form);
						s.headParts.emplace(a_id, HeadPartState{ part, part->textureSet, Str(part->GetModel()) });
					}
					break;
				case RE::FormType::TextureSet:
					if (a_details) {
						const auto* set = static_cast<const RE::BGSTextureSet*>(a_form);
						TextureSetState st{ set };
						for (std::size_t i = 0; i < st.paths.size(); ++i) {
							st.paths[i] = Str(set->textures[i].textureName.c_str());
						}
						s.textureSets.emplace(a_id, std::move(st));
					}
					break;
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

		using EmptiedSet = std::unordered_set<const RE::TESForm*>;

		// The revert resets some NPC records to blank defaults instead of
		// reloading them: the characters loaded during the lobby's game (the
		// Bannered Mare's Mikael and Uthgerd, a mod's Lydia standing there).
		// Its mark is that every spell and perk is gone: a save can change an
		// NPC's look, outfit or weight, but not empty its base record's lists.
		// The record's head data, face data and tint list are freed but may
		// still be pointed at, so this looks only at the lists, never into
		// those blocks.
		bool ListsGone(const NpcState& a_st)
		{
			const auto* npc = a_st.npc;
			if (a_st.spells.size() + a_st.levSpells.size() + a_st.shouts.size() + a_st.perks.size() == 0) {
				return false;
			}
			const auto* d = npc->actorEffects;
			return (!d || d->numSpells + d->numlevSpells + d->numShouts == 0) && npc->perkCount == 0;
		}

		// The emptied records: those whose lists are gone, and the face
		// templates they borrow their look from (followed through the captured
		// faceNPC chain), which come back emptied with them but have no lists
		// to show it. Worked out once, before anything is restored: a restored
		// record has its lists back, so a later Restore() with the same capture
		// (after MAIN MENU, then a load) never takes it for emptied again.
		EmptiedSet FindEmptied(const Snapshot& a_s)
		{
			EmptiedSet out;
			for (const auto& [id, st] : a_s.npcs) {
				if (!ListsGone(st)) {
					continue;
				}
				const RE::TESNPC* npc = st.npc;
				while (npc && out.insert(npc).second) {
					const auto it = a_s.npcs.find(npc->GetFormID());
					npc = it != a_s.npcs.end() ? it->second.faceNPC : nullptr;
				}
			}
			return out;
		}

		// Moves a_fresh into a_array without freeing a_array's buffer: another
		// thread may be reading it (building an actor's 3D from an armor), or,
		// on an emptied record, it may already be freed. The old buffer is
		// left as it is, a few bytes per record.
		template <class T>
		void InstallFresh(RE::BSTArray<T>& a_array, RE::BSTArray<T>&& a_fresh)
		{
			std::construct_at(&a_array, std::move(a_fresh));
		}

		// Armor addons the revert removed (the armor holds only some of what it
		// held), added back in their captured order. Anything a mod has added
		// or swapped since is left alone.
		void RestoreArmors(const Snapshot& a_s, Fixes& a_fixes)
		{
			for (const auto& [id, st] : a_s.armors) {
				auto&      now = st.armor->armorAddons;
				const auto missing = Missing(now.data(), now.size(), st.addons);
				if (missing.empty()) {
					continue;
				}
				const auto                       had = now.size();
				RE::BSTArray<RE::TESObjectARMA*> fresh(now);
				for (auto* addon : missing) {
					fresh.push_back(addon);
				}
				InstallFresh(now, std::move(fresh));
				// The body slots the captured addons cover, which the revert also reset.
				st.armor->bipedModelData.bipedObjectSlots.set(static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(st.slots));
				a_fixes.Add("armor addons", std::format("{} {}->{}", Describe(st.armor), had, now.size()));
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

		// A copy of a_items in a fresh engine array; the old one is never read
		// or freed (an emptied record's may already be).
		template <class T>
		T** FreshArray(const std::vector<T*>& a_items)
		{
			auto* out = RE::calloc<T*>(a_items.size());
			std::ranges::copy(a_items, out);
			return out;
		}

		void RestoreKeywords(const Snapshot& a_s, const EmptiedSet& a_emptied, Fixes& a_fixes)
		{
			for (const auto& [id, st] : a_s.keywords) {
				if (a_emptied.contains(st.owner)) {
					if (st.form->numKeywords == 0 && !st.keywords.empty()) {
						st.form->keywords = FreshArray(st.keywords);
						st.form->numKeywords = static_cast<std::uint32_t>(st.keywords.size());
						a_fixes.Add("NPC keywords", std::format("{} +{}", Describe(st.owner), st.keywords.size()));
					}
					continue;
				}
				const auto missing = Missing(st.form->keywords, st.form->numKeywords, st.keywords);
				if (!missing.empty()) {
					st.form->AddKeywords(missing);
					a_fixes.Add(std::format("{} keywords", TypeName(st.owner)), std::format("{} +{}", Describe(st.owner), missing.size()));
				}
			}
		}

		// The rest of an emptied record. Fields and lists are only filled where
		// the reset left them empty: a value there now came from the save or a
		// mod, and stays. Height the reset sets to 1; weight it leaves alone,
		// so weight isn't touched. Its head data, head parts, tint list and
		// face data are freed blocks the record may still point at, and its
		// faction and inventory lists may be too: each gets a fresh copy of the
		// captured values, and the old block is never read, written or freed.
		void RestoreEmptied(const NpcState& a_st, Fixes& a_fixes)
		{
			auto*                    npc = a_st.npc;
			std::vector<std::string> what;
			auto fill = [&](auto& a_field, auto a_value, std::string_view a_name) {
				if (!a_field && a_value) {
					a_field = a_value;
					what.emplace_back(a_name);
				}
			};
			fill(npc->faceNPC, a_st.faceNPC, "face template");
			fill(npc->npcClass, a_st.npcClass, "class");
			fill(npc->combatStyle, a_st.combatStyle, "combat style");
			fill(npc->voiceType, a_st.voiceType, "voice");
			fill(npc->defaultOutfit, a_st.defaultOutfit, "outfit");
			fill(npc->sleepOutfit, a_st.sleepOutfit, "sleep outfit");
			if (npc->height == 1.0f && a_st.height != 1.0f) {
				npc->height = a_st.height;
				what.emplace_back("height");
			}

			if (npc->factions.empty() && !a_st.factions.empty()) {
				RE::BSTArray<RE::FACTION_RANK> fresh;
				for (const auto& [faction, rank] : a_st.factions) {
					RE::FACTION_RANK entry{};
					entry.faction = faction;
					entry.rank = rank;
					fresh.push_back(entry);
				}
				InstallFresh(npc->factions, std::move(fresh));
				what.push_back(std::format("{} factions", a_st.factions.size()));
			}
			if (npc->aiPackages.packages.empty() && !a_st.packages.empty()) {
				for (auto it = a_st.packages.rbegin(); it != a_st.packages.rend(); ++it) {
					npc->aiPackages.packages.push_front(*it);
				}
				what.push_back(std::format("{} AI packages", a_st.packages.size()));
			}
			if (npc->numContainerObjects == 0 && !a_st.items.empty()) {
				std::vector<RE::ContainerObject*> entries;
				for (const auto& [obj, count] : a_st.items) {
					entries.push_back(new RE::ContainerObject(obj, count));
				}
				npc->containerObjects = FreshArray(entries);
				npc->numContainerObjects = static_cast<std::uint32_t>(entries.size());
				what.push_back(std::format("{} inventory entries", entries.size()));
			}
			if (a_st.hadHead) {
				auto* head = new RE::TESNPC::HeadRelatedData();
				head->hairColor = a_st.hairColor;
				head->faceDetails = a_st.faceDetails;
				npc->headRelatedData = head;
				what.emplace_back("hair color and face details");
			}
			if (!a_st.headParts.empty()) {
				npc->headParts = FreshArray(a_st.headParts);
				npc->numHeadParts = static_cast<std::int8_t>(a_st.headParts.size());
				what.push_back(std::format("{} head parts", a_st.headParts.size()));
			}
			if (!a_st.tints.empty()) {
				npc->tintLayers = std::construct_at(RE::calloc<RE::BSTArray<RE::TESNPC::Layer*>>(1));
				for (const auto& layer : a_st.tints) {
					auto* copy = RE::calloc<RE::TESNPC::Layer>(1);
					*copy = layer;
					npc->tintLayers->push_back(copy);
				}
				what.push_back(std::format("{} skin tints", a_st.tints.size()));
			}
			if (a_st.faceData) {
				auto* face = RE::calloc<RE::TESNPC::FaceData>(1);
				*face = *a_st.faceData;
				npc->faceData = face;
				what.emplace_back("face shape");
			}
			logger::debug("ResetGuard:   emptied record {}: put back {}", Describe(npc), what.empty() ? "nothing else" : Join(what));
			a_fixes.Add("emptied NPC records", std::format("{} ({})", Describe(npc), what.empty() ? "nothing else differed" : Join(what)));
		}

		// a_was's perks missing from the record, with their captured ranks.
		std::vector<RE::PerkRankData> MissingPerks(const RE::TESNPC* a_npc, const NpcState& a_st)
		{
			std::vector<RE::BGSPerk*> now;
			for (std::uint32_t i = 0; a_npc->perks && i < a_npc->perkCount; ++i) {
				now.push_back(a_npc->perks[i].perk);
			}
			std::vector<RE::BGSPerk*> was;
			for (const auto& [perk, rank] : a_st.perks) {
				was.push_back(perk);
			}
			const auto missing = Missing(now.data(), now.size(), was);
			std::vector<RE::PerkRankData> out;
			for (const auto& [perk, rank] : a_st.perks) {
				if (std::ranges::find(missing, perk) != missing.end()) {
					out.emplace_back(perk, rank);
				}
			}
			return out;
		}

		// Spells, perks and head parts that went missing, then the rest of any
		// record the revert emptied. An emptied record's spell and perk lists
		// are rebuilt in fresh blocks (CommonLib's Add* helpers free the old
		// array, which may already be freed); others are added to in place.
		void RestoreNpcs(const Snapshot& a_s, const EmptiedSet& a_emptied, Fixes& a_fixes)
		{
			for (const auto& [id, st] : a_s.npcs) {
				auto*      npc = st.npc;
				const bool emptied = a_emptied.contains(npc);
				if (emptied && (!st.spells.empty() || !st.levSpells.empty() || !st.shouts.empty())) {
					npc->actorEffects = new RE::TESSpellList::SpellData();
				}
				auto*      d = npc->actorEffects;
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

				if (emptied && !st.perks.empty()) {
					npc->perks = nullptr;
					npc->perkCount = 0;
				}
				if (const auto missing = MissingPerks(npc, st); !missing.empty()) {
					auto* old = npc->perks;
					auto* fresh = RE::calloc<RE::PerkRankData>(npc->perkCount + missing.size());
					std::copy_n(npc->perks, npc->perkCount, fresh);
					std::ranges::copy(missing, fresh + npc->perkCount);
					npc->perks = fresh;
					npc->perkCount += static_cast<std::uint32_t>(missing.size());
					RE::free(old);  // null for an emptied record
					a_fixes.Add("NPC perks", std::format("{} +{}", Describe(npc), missing.size()));
				}

				if (emptied) {
					RestoreEmptied(st, a_fixes);
					continue;
				}

				const auto partsNow = npc->numHeadParts > 0 ? static_cast<std::size_t>(npc->numHeadParts) : 0;
				if (const auto missing = Missing(npc->headParts, partsNow, st.headParts); !missing.empty()) {
					npc->headParts = FreshArray(st.headParts);  // the old, shorter array is left as it is
					npc->numHeadParts = static_cast<std::int8_t>(st.headParts.size());
					a_fixes.Add("NPC head parts", std::format("{} {}->{}", Describe(npc), partsNow, st.headParts.size()));
				}

				// Hair color and face details missing from a record that wasn't
				// emptied: Papyrus GetHairColor crashed on a missing one.
				if (st.hairColor || st.faceDetails) {
					auto*      head = npc->headRelatedData;
					const bool hair = st.hairColor && !(head && head->hairColor);
					const bool face = st.faceDetails && !(head && head->faceDetails);
					if (hair || face) {
						if (!head) {
							npc->headRelatedData = head = new RE::TESNPC::HeadRelatedData();
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
				logger::debug("ResetGuard: {}: nothing else changed across the switch", a_when);
				return;
			}
			std::size_t shown = 0;
			for (const auto& [what, entry] : changes.byKind) {
				if (++shown > kMaxListed) {
					logger::debug("ResetGuard: {}: ({} more kinds of change not listed)", a_when, changes.byKind.size() - kMaxListed);
					break;
				}
				logger::debug("ResetGuard: {}: {} on {} records, e.g. {}", a_when, what, entry.first, Join(entry.second));
			}
		}
	}

	bool Capture(std::string_view a_why)
	{
		if (g_capture) {
			logger::info("ResetGuard: leaving the lobby's game ({}); keeping the capture from '{}'", a_why, g_capture->why);
			return false;
		}
		const auto start = std::chrono::steady_clock::now();
		g_capture = Take(a_why, spdlog::should_log(spdlog::level::debug));
		logger::info("ResetGuard: leaving the lobby's game ({}): captured {} armors, {} skins, {} keyword lists, {} NPCs ({:.1f} ms)", a_why,
			g_capture->armors.size(), g_capture->skins.size(), g_capture->keywords.size(), g_capture->npcs.size(),
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		return true;
	}

	void Discard(std::string_view a_why)
	{
		if (g_capture) {
			logger::info("ResetGuard: the lobby's game goes on ({}); capture from '{}' dropped", a_why, g_capture->why);
			g_capture.reset();
		}
	}

	void Restore(std::string_view a_when, bool a_gameStarted)
	{
		if (!g_capture) {
			return;
		}
		Fixes      fixes;
		const auto emptied = FindEmptied(*g_capture);
		RestoreArmors(*g_capture, fixes);
		RestoreSkins(*g_capture, fixes);
		RestoreKeywords(*g_capture, emptied, fixes);
		RestoreNpcs(*g_capture, emptied, fixes);
		logger::info("ResetGuard: {} (left the lobby's game: {}): {} fixes", a_when, g_capture->why, fixes.Total());
		for (const auto& [kind, entry] : fixes.byKind) {
			logger::info("ResetGuard:   put back {} on {} records, e.g. {}", kind, entry.first, Join(entry.second));
		}
		// What else the switch changed, after the restore: by now no record
		// points at a freed block (debug log only).
		if (spdlog::should_log(spdlog::level::debug)) {
			ReportOtherChanges(*g_capture, a_when);
		}
		if (a_gameStarted) {
			g_capture.reset();
		}
	}
}
