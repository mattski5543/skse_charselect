// Skin and armor-addon snapshots for the missing-hands investigation (see
// SkinAudit.h).

#include "SkinAudit.h"

#include "Util.h"
#include "core/Encoding.h"

namespace charselect::skinaudit
{
	namespace
	{
		constexpr std::size_t   kMaxListed = 25;  // per section, so one big change can't flood the log
		constexpr std::uint32_t kBodyBit = 1u << 2;
		constexpr std::uint32_t kHandsBit = 1u << 3;

		struct ArmorState
		{
			const RE::TESObjectARMO*              form{ nullptr };
			std::vector<const RE::TESObjectARMA*> addons;
		};

		// Pointers in a snapshot are only compared. A form is dereferenced only
		// while the snapshot being reported shows it registered (live), so a
		// freed addon still listed by a skin is reported, not read.
		struct Snapshot
		{
			std::unordered_map<RE::FormID, ArmorState>               armors;
			std::unordered_map<RE::FormID, const RE::TESObjectARMO*> skins;  // NPC or race ID -> its skin
			std::unordered_map<RE::FormID, const RE::TESForm*>       owners;  // NPC or race ID -> the form
			std::unordered_map<const RE::TESForm*, RE::FormID>       live;   // every registered armor and addon
			std::size_t                                              addons{ 0 };
			std::size_t                                              dynamicArmors{ 0 };
			std::size_t                                              dynamicAddons{ 0 };
		};

		std::optional<Snapshot> g_baseline;

		Snapshot Take()
		{
			Snapshot   s;
			const auto& [map, lock] = RE::TESForm::GetAllForms();
			const RE::BSReadLockGuard guard{ lock };
			if (!map) {
				return s;
			}
			for (const auto& [id, form] : *map) {
				if (!form) {
					continue;
				}
				switch (form->GetFormType()) {
				case RE::FormType::Armor:
					{
						const auto* armor = static_cast<const RE::TESObjectARMO*>(form);
						s.armors.emplace(id, ArmorState{ armor, { armor->armorAddons.begin(), armor->armorAddons.end() } });
						s.live.emplace(form, id);
						if (form->IsDynamicForm()) {
							++s.dynamicArmors;
						}
						break;
					}
				case RE::FormType::Armature:
					s.live.emplace(form, id);
					++s.addons;
					if (form->IsDynamicForm()) {
						++s.dynamicAddons;
					}
					break;
				case RE::FormType::Race:
					s.skins.emplace(id, static_cast<const RE::TESRace*>(form)->skin);
					s.owners.emplace(id, form);
					break;
				case RE::FormType::NPC:
					s.skins.emplace(id, static_cast<const RE::TESNPC*>(form)->skin);
					s.owners.emplace(id, form);
					break;
				default:
					break;
				}
			}
			return s;
		}

		std::string Slots(std::uint32_t a_mask)
		{
			std::string out;
			for (std::uint32_t bit = 0; bit < 32; ++bit) {
				if (a_mask & (1u << bit)) {
					out += std::format("{}{}", out.empty() ? "" : ",", 30 + bit);
				}
			}
			return out.empty() ? "none" : out;
		}

		std::uint32_t Mask(const RE::BGSBipedObjectForm& a_form)
		{
			return a_form.bipedModelData.bipedObjectSlots.underlying();
		}

		// "0001A2B3 EditorID [Plugin.esp]"
		std::string Describe(const RE::TESForm* a_form)
		{
			std::string out = std::format("{:08X}", a_form->GetFormID());
			if (const char* edid = a_form->GetFormEditorID(); edid && *edid) {
				out += " " + text::ToUtf8(edid);
			}
			if (const auto owner = OwningPlugin(a_form)) {
				out += std::format(" [{}]", text::ToUtf8(owner->file->GetFilename()));
			} else if (a_form->IsDynamicForm()) {
				out += " [runtime]";
			}
			return out;
		}

		// A form from either snapshot: described if registered now, otherwise
		// the ID it had when last seen.
		std::string DescribeAny(const Snapshot& a_now, const RE::TESForm* a_form)
		{
			if (!a_form) {
				return "none";
			}
			if (a_now.live.contains(a_form)) {
				return Describe(a_form);
			}
			if (g_baseline) {
				if (const auto it = g_baseline->live.find(a_form); it != g_baseline->live.end()) {
					return std::format("{:08X} (NO LONGER REGISTERED: freed or replaced)", it->second);
				}
			}
			return std::format("{} (NOT A REGISTERED FORM)", static_cast<const void*>(a_form));
		}

		std::string DescribeAddon(const RE::TESObjectARMA* a_addon)
		{
			const char* model = a_addon->bipedModels[RE::SEXES::kMale].GetModel();
			return std::format("{} slots {} male '{}' race {} (+{} more)", Describe(a_addon), Slots(Mask(*a_addon)),
				model ? text::ToUtf8(model) : "", a_addon->race ? std::format("{:08X}", a_addon->race->GetFormID()) : "none",
				a_addon->additionalRaces.size());
		}

		void LogArmor(const Snapshot& a_now, const ArmorState& a_state, const ArmorState* a_was, std::string_view a_why)
		{
			const auto* armor = a_state.form;
			logger::info("SkinAudit:   armor {} slots {}, {} addons ({})", Describe(armor), Slots(Mask(*armor)), a_state.addons.size(), a_why);
			bool hands = false;
			for (const auto* addon : a_state.addons) {
				const bool before = a_was && std::ranges::find(a_was->addons, addon) != a_was->addons.end();
				const auto mark = before ? "="sv : "+"sv;
				if (!addon || !a_now.live.contains(addon)) {
					logger::info("SkinAudit:     {} {}", mark, DescribeAny(a_now, addon));
					continue;
				}
				const char* model = addon->bipedModels[RE::SEXES::kMale].GetModel();
				if ((Mask(*addon) & kHandsBit) && model && *model) {
					hands = true;
				}
				logger::info("SkinAudit:     {} {}", mark, DescribeAddon(addon));
			}
			if (a_was) {
				for (const auto* old : a_was->addons) {
					if (std::ranges::find(a_state.addons, old) == a_state.addons.end()) {
						logger::info("SkinAudit:     - {} (removed)", DescribeAny(a_now, old));
					}
				}
			}
			if ((Mask(*armor) & kBodyBit) && !hands) {
				logger::info("SkinAudit:     NO MALE HANDS ADDON on this armor");
			}
		}

		std::string ActorName(const RE::TESForm* a_form)
		{
			const auto* named = a_form->As<RE::TESFullName>();
			const char* name = named ? named->GetFullName() : nullptr;
			return name && *name ? text::ToUtf8(name) : "<no name>";
		}

		std::string WornIn(const Snapshot& a_now, const RE::BIPOBJECT& a_object)
		{
			const auto* item = a_object.item;
			return std::format("item {} addon {} 3D {}", DescribeAny(a_now, item), DescribeAny(a_now, a_object.addon),
				a_object.partClone ? "yes" : "NO");
		}

		void LogLoadedMen(const Snapshot& a_now)
		{
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists) {
				return;
			}
			std::size_t listed = 0;
			for (const auto& handle : lists->highActorHandles) {
				const auto actor = handle.get();
				if (!actor || !actor->Is3DLoaded()) {
					continue;
				}
				const auto* base = actor->GetActorBase();
				if (!base || base->IsFemale()) {
					continue;
				}
				if (++listed > kMaxListed * 2) {
					logger::info("SkinAudit:   (more men not listed)");
					break;
				}
				const auto* race = actor->GetRace();
				logger::info("SkinAudit:   man '{}' ref {:08X} base {} race {} skin {}", ActorName(base), actor->GetFormID(), Describe(base),
					race ? Describe(race) : "none", DescribeAny(a_now, actor->GetSkin()));
				const auto& biped = actor->GetBiped(false);
				if (!biped) {
					logger::info("SkinAudit:     no biped");
					continue;
				}
				logger::info("SkinAudit:     hands: {}", WornIn(a_now, biped->objects[RE::BIPED_OBJECTS::kHands]));
				logger::info("SkinAudit:     body:  {}", WornIn(a_now, biped->objects[RE::BIPED_OBJECTS::kBody]));
			}
			logger::info("SkinAudit:   {} loaded men", (std::min)(listed, kMaxListed * 2));
		}

		// The watch after a load: every loaded man whose skin is one of a skin
		// mod's own (filled at runtime) or has no addons at all, or whose
		// hands have no 3D, logged once per change, so the log names who should
		// be missing hands. Reads only loaded actors' current forms.
		constexpr auto kWatchInterval = std::chrono::seconds(20);
		constexpr int  kWatchTicks = 45;  // 15 minutes after each load
		std::uint32_t  g_watchGeneration{ 0 };
		std::unordered_map<RE::FormID, std::string> g_watched;  // actor ref -> last line logged

		bool IsRuntimeSkin(const RE::TESObjectARMO* a_skin)
		{
			const auto owner = OwningPlugin(a_skin);
			return owner && owner->file->GetFilename() == "SkinMod.esp"sv;
		}

		std::string Worn(const RE::BIPOBJECT& a_object)
		{
			return std::format("item {} addon {} 3D {}", a_object.item ? Describe(a_object.item) : "none",
				a_object.addon ? Describe(a_object.addon) : "none", a_object.partClone ? "yes" : "NO");
		}

		void WatchMen(std::uint32_t a_generation, int a_tick)
		{
			if (a_generation != g_watchGeneration || a_tick >= kWatchTicks) {
				return;
			}
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				for (const auto& handle : lists->highActorHandles) {
					const auto actor = handle.get();
					if (!actor || !actor->Is3DLoaded()) {
						continue;
					}
					const auto* base = actor->GetActorBase();
					const auto* skin = actor->GetSkin();
					const auto& biped = actor->GetBiped(false);
					if (!base || base->IsFemale() || !skin || !biped) {
						continue;
					}
					const auto& hands = biped->objects[RE::BIPED_OBJECTS::kHands];
					if (!IsRuntimeSkin(skin) && !skin->armorAddons.empty() && hands.partClone) {
						continue;
					}
					auto line = std::format("man '{}' ref {:08X} base {} skin {} with {} addons; hands: {}", ActorName(base), actor->GetFormID(),
						Describe(base), Describe(skin), skin->armorAddons.size(), Worn(hands));
					auto& last = g_watched[actor->GetFormID()];
					if (last != line) {
						logger::info("SkinAudit: watch: {}", line);
						last = std::move(line);
					}
				}
			}
			RunAfter(kWatchInterval, [a_generation, a_tick]() { WatchMen(a_generation, a_tick + 1); });
		}

		void Report(const Snapshot& a_now, std::string_view a_when)
		{
			const auto& was = *g_baseline;
			logger::info("SkinAudit: {}: {} armors ({} runtime), {} addons ({} runtime); at the main menu {} ({}), {} ({})", a_when,
				a_now.armors.size(), a_now.dynamicArmors, a_now.addons, a_now.dynamicAddons, was.armors.size(), was.dynamicArmors,
				was.addons, was.dynamicAddons);

			// NPCs and races whose skin differs from the main menu's, grouped by
			// (skin then, skin now). New forms (leveled actors, the lobby's
			// characters) count as having had none.
			struct Group
			{
				std::size_t              count{ 0 };
				std::vector<std::string> examples;
			};
			std::map<std::pair<const RE::TESObjectARMO*, const RE::TESObjectARMO*>, Group> groups;
			for (const auto& [id, skin] : a_now.skins) {
				const auto it = was.skins.find(id);
				const auto* before = it != was.skins.end() ? it->second : nullptr;
				if (before == skin) {
					continue;
				}
				auto& g = groups[{ before, skin }];
				if (++g.count <= 3) {
					const auto* owner = a_now.owners.at(id);
					g.examples.push_back(std::format("{} '{}'{}", Describe(owner), ActorName(owner), it == was.skins.end() ? " (new form)" : ""));
				}
			}
			logger::info("SkinAudit: {}: {} skin changes on NPCs and races", a_when, groups.size());
			std::set<const RE::TESObjectARMO*> newSkins;
			std::size_t                        shown = 0;
			for (const auto& [key, g] : groups) {
				if (++shown > kMaxListed) {
					logger::info("SkinAudit:   ({} more not listed)", groups.size() - kMaxListed);
					break;
				}
				logger::info("SkinAudit:   {} -> {}: {} forms, e.g. {}", DescribeAny(a_now, key.first), DescribeAny(a_now, key.second), g.count,
					[&]() {
						std::string all;
						for (const auto& e : g.examples) {
							all += (all.empty() ? "" : "; ") + e;
						}
						return all;
					}());
				if (key.second && a_now.live.contains(key.second)) {
					newSkins.insert(key.second);
				}
			}

			// Armors whose addon list changed, or whose ID now holds another form.
			std::size_t changed = 0, added = 0;
			for (const auto& [id, state] : a_now.armors) {
				const auto it = was.armors.find(id);
				if (it == was.armors.end()) {
					++added;
					continue;
				}
				if (it->second.form == state.form && it->second.addons == state.addons) {
					continue;
				}
				if (++changed <= kMaxListed) {
					LogArmor(a_now, state, &it->second, it->second.form == state.form ? "addons changed" : "a different form under this ID");
				}
				newSkins.erase(state.form);
			}
			logger::info("SkinAudit: {}: {} armors changed since the main menu, {} new", a_when, changed, added);

			// Skins now worn by NPCs or races that weren't listed above: what
			// they hold, especially any new runtime skin.
			shown = 0;
			for (const auto* skin : newSkins) {
				const auto it = a_now.armors.find(skin->GetFormID());
				if (it == a_now.armors.end() || it->second.form != skin) {
					continue;
				}
				if (++shown > kMaxListed) {
					break;
				}
				const auto wasIt = was.armors.find(skin->GetFormID());
				const bool wasThere = wasIt != was.armors.end() && wasIt->second.form == skin;
				LogArmor(a_now, it->second, wasThere ? &wasIt->second : nullptr, wasThere ? "unchanged, newly worn" : "new since the main menu");
			}
		}
	}

	void Checkpoint(std::string_view a_when, bool a_men)
	{
		const auto start = std::chrono::steady_clock::now();
		auto       now = Take();
		if (!g_baseline) {
			logger::info("SkinAudit: baseline at {}: {} armors ({} runtime), {} addons ({} runtime), {} NPCs and races", a_when,
				now.armors.size(), now.dynamicArmors, now.addons, now.dynamicAddons, now.skins.size());
			g_baseline = std::move(now);
			return;
		}
		Report(now, a_when);
		if (a_men) {
			LogLoadedMen(now);
		}
		logger::info("SkinAudit: {} done in {:.1f} ms", a_when,
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
	}

	void CheckpointAfter(std::chrono::seconds a_delay, std::string a_when, bool a_men)
	{
		RunAfter(a_delay, [when = std::move(a_when), a_men]() { Checkpoint(when, a_men); });
	}

	void StartWatch()
	{
		const auto generation = ++g_watchGeneration;
		g_watched.clear();
		logger::info("SkinAudit: watching loaded men for {} min", kWatchTicks * kWatchInterval.count() / 60);
		WatchMen(generation, 0);
	}
}
