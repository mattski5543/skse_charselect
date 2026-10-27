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
			RE::TESObjectARMO*               armor{ nullptr };
			std::vector<RE::TESObjectARMA*>  addons;
			std::uint32_t                    slots{ 0 };
		};

		struct SkinState
		{
			const RE::TESForm* owner{ nullptr };  // the NPC or race
			RE::BGSSkinForm*   skinForm{ nullptr };
			RE::TESObjectARMO* skin{ nullptr };
		};

		// What else is compared across the switch, logged only.
		struct Counts
		{
			std::uint32_t          keywords{ 0 };
			std::uint32_t          spells{ 0 };
			std::uint32_t          perks{ 0 };
			std::uint32_t          factions{ 0 };
			const RE::BGSOutfit*   outfit{ nullptr };
		};

		// Only plugin records, and only pointers to plugin records: those are
		// never freed, so they're safe to keep and write back.
		struct Snapshot
		{
			std::string                                                      why;
			std::unordered_map<RE::FormID, ArmorState>                       armors;
			std::unordered_map<RE::FormID, SkinState>                        skins;
			std::unordered_map<RE::FormID, std::pair<const RE::TESForm*, Counts>> counts;
		};

		std::optional<Snapshot> g_capture;

		bool IsPluginForm(const RE::TESForm* a_form)
		{
			return a_form && !a_form->IsDynamicForm();
		}

		std::uint32_t SpellCount(const RE::TESSpellList& a_list)
		{
			const auto* d = a_list.actorEffects;
			return d ? d->numSpells + d->numlevSpells + d->numShouts : 0;
		}

		std::optional<Counts> CountsOf(const RE::TESForm* a_form)
		{
			switch (a_form->GetFormType()) {
			case RE::FormType::NPC:
				{
					const auto* npc = static_cast<const RE::TESNPC*>(a_form);
					return Counts{ npc->numKeywords, SpellCount(*npc), npc->perkCount, npc->factions.size(), npc->defaultOutfit };
				}
			case RE::FormType::Race:
				{
					const auto* race = static_cast<const RE::TESRace*>(a_form);
					return Counts{ race->numKeywords, SpellCount(*race), 0, 0, nullptr };
				}
			case RE::FormType::Armor:
				return Counts{ static_cast<const RE::TESObjectARMO*>(a_form)->numKeywords };
			case RE::FormType::Weapon:
				return Counts{ static_cast<const RE::TESObjectWEAP*>(a_form)->numKeywords };
			default:
				return std::nullopt;
			}
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

		Snapshot Take(std::string_view a_why)
		{
			Snapshot c{ std::string(a_why) };
			ForEachForm([&](RE::FormID a_id, RE::TESForm* a_form) {
				switch (a_form->GetFormType()) {
				case RE::FormType::Armor:
					{
						auto* armor = static_cast<RE::TESObjectARMO*>(a_form);
						ArmorState st{ armor, { armor->armorAddons.begin(), armor->armorAddons.end() },
							armor->bipedModelData.bipedObjectSlots.underlying() };
						if (std::ranges::all_of(st.addons, IsPluginForm)) {
							c.armors.emplace(a_id, std::move(st));
						}
						break;
					}
				case RE::FormType::NPC:
				case RE::FormType::Race:
					{
						auto* skinForm = a_form->As<RE::BGSSkinForm>();
						if (skinForm && IsPluginForm(skinForm->skin)) {
							c.skins.emplace(a_id, SkinState{ a_form, skinForm, skinForm->skin });
						}
						break;
					}
				default:
					break;
				}
				if (const auto counts = CountsOf(a_form)) {
					c.counts.emplace(a_id, std::pair{ a_form, *counts });
				}
			});
			return c;
		}

		// Armor addons the revert removed: the armor now holds only some of
		// what it held, nothing new. Anything a mod has added or swapped since
		// is left alone.
		std::size_t RestoreArmors(const Snapshot& a_capture)
		{
			std::size_t restored = 0;
			for (const auto& [id, st] : a_capture.armors) {
				auto& now = st.armor->armorAddons;
				if (now.size() >= st.addons.size() ||
					!std::ranges::all_of(now, [&](const RE::TESObjectARMA* a) { return std::ranges::find(st.addons, a) != st.addons.end(); })) {
					continue;
				}
				const auto had = now.size();
				now.clear();
				for (auto* addon : st.addons) {
					now.push_back(addon);
				}
				st.armor->bipedModelData.bipedObjectSlots = SlotMask(static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(st.slots));
				if (++restored <= kMaxListed) {
					logger::info("ResetGuard:   armor {}: {} of {} addons were left; put back", Describe(st.armor), had, st.addons.size());
				}
			}
			return restored;
		}

		// NPC and race skins the revert emptied. A skin that changed to another
		// skin is left alone (a mod may have assigned it since).
		std::size_t RestoreSkins(const Snapshot& a_capture)
		{
			std::map<const RE::TESObjectARMO*, std::vector<const RE::TESForm*>> bySkin;
			for (const auto& [id, st] : a_capture.skins) {
				if (st.skinForm->skin) {
					continue;
				}
				st.skinForm->skin = st.skin;
				bySkin[st.skin].push_back(st.owner);
			}
			std::size_t restored = 0, shown = 0;
			for (const auto& [skin, owners] : bySkin) {
				restored += owners.size();
				if (++shown > kMaxListed) {
					continue;
				}
				std::string examples;
				for (std::size_t i = 0; i < (std::min)(owners.size(), kMaxExamples); ++i) {
					examples += (examples.empty() ? "" : "; ") + Describe(owners[i]);
				}
				logger::info("ResetGuard:   skin {} put back on {} {}: {}", Describe(skin), owners.size(), owners.size() == 1 ? "form" : "forms", examples);
			}
			return restored;
		}

		// Everything else compared, logged only: which records came out of the
		// switch with fewer or more keywords, spells, perks or factions, or a
		// different outfit.
		void ReportOtherChanges(const Snapshot& a_capture, std::string_view a_when)
		{
			struct Tally
			{
				std::size_t              count{ 0 };
				std::vector<std::string> examples;
			};
			std::map<std::string, Tally> tallies;
			auto note = [&](const RE::TESForm* a_form, std::string_view a_what, std::uint32_t a_was, std::uint32_t a_now) {
				if (a_was == a_now) {
					return;
				}
				auto& t = tallies[std::format("{} {} {}", TypeName(a_form), a_what, a_now < a_was ? "fewer" : "more")];
				if (++t.count <= kMaxExamples) {
					t.examples.push_back(std::format("{} {}->{}", Describe(a_form), a_was, a_now));
				}
			};
			for (const auto& [id, entry] : a_capture.counts) {
				const auto& [form, was] = entry;
				if (RE::TESForm::LookupByID(id) != form) {
					continue;
				}
				const auto now = CountsOf(form);
				if (!now) {
					continue;
				}
				note(form, "keywords", was.keywords, now->keywords);
				note(form, "spells", was.spells, now->spells);
				note(form, "perks", was.perks, now->perks);
				note(form, "factions", was.factions, now->factions);
				if (was.outfit != now->outfit) {
					auto& t = tallies[std::format("{} outfit changed", TypeName(form))];
					if (++t.count <= kMaxExamples) {
						t.examples.push_back(Describe(form));
					}
				}
			}
			if (tallies.empty()) {
				logger::info("ResetGuard: {}: no keyword, spell, perk, faction or outfit changes across the switch", a_when);
				return;
			}
			for (const auto& [what, t] : tallies) {
				std::string examples;
				for (const auto& e : t.examples) {
					examples += (examples.empty() ? "" : "; ") + e;
				}
				logger::info("ResetGuard: {}: {} on {} records, e.g. {}", a_when, what, t.count, examples);
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
		logger::info("ResetGuard: leaving the lobby's game ({}): captured {} armors, {} skins, {} records to compare ({:.1f} ms)", a_why,
			g_capture->armors.size(), g_capture->skins.size(), g_capture->counts.size(),
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
	}

	void Restore(std::string_view a_when, bool a_gameStarted)
	{
		if (!g_capture) {
			return;
		}
		const auto armors = RestoreArmors(*g_capture);
		const auto skins = RestoreSkins(*g_capture);
		logger::info("ResetGuard: {} (left the lobby's game: {}): put back the addons of {} armors and {} skins", a_when, g_capture->why, armors,
			skins);
		ReportOtherChanges(*g_capture, a_when);
		if (a_gameStarted) {
			g_capture.reset();
		}
	}
}
