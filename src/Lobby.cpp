// The lobby itself: starting the throwaway game, placing the characters, the
// layout editor, and loading the save the player picks (see Lobby.h).

#include "Lobby.h"

#include "Config.h"
#include "Papyrus.h"
#include "Layout.h"
#include "Paths.h"
#include "Poses.h"
#include "SelectMenu.h"
#include "Tint.h"
#include "Util.h"
#include "core/Encoding.h"
#include "core/FormKey.h"
#include "core/LayoutMath.h"

namespace charselect::lobby
{
	namespace
	{
		// CharGen ApplyTypes: face | overrides | body morphs | transforms | skin overrides
		constexpr std::uint32_t kPresetApplyAll = 0x0F;
		constexpr RE::FormID    kPlayerBaseId = 0x7;
		// Skyrim.esm BardAudienceExcludedFaction: BardAudienceQuest's bystander
		// aliases skip its members. Without it, a bard's song in the lobby cell
		// pulls our characters off their spots to stand and applaud (L8).
		constexpr RE::FormID    kBardAudienceExcludedFaction = 0x10FCB4;

		// After the loading screen closes, wait for the player to be settled in
		// the lobby cell before staging (the first close comes before coc has
		// finished placing the player). TrySpawn polls for that itself; 1.5 s
		// here waited on a player who was ready at once (0.5.6 log).
		constexpr auto kSettleDelay = std::chrono::milliseconds(500);
		constexpr auto kReadyPollInterval = std::chrono::milliseconds(250);
		constexpr int  kReadyPollMax = 40;                            // 10 s
		constexpr auto kEnterWatchdog = std::chrono::seconds(30);     // coc never loaded anything
		constexpr auto kRevealTimeout = std::chrono::seconds(10);     // show the menu even if a slot is slow
		constexpr auto kRevealSettle = std::chrono::milliseconds(500);  // poses in place -> let idles blend in, then show
		constexpr auto kFreezeDelay = std::chrono::milliseconds(600);  // let actors settle on the floor first (was 1.2 s)

		// The game-free code (core/) uses Vec3.
		Vec3         ToVec(const RE::NiPoint3& a_v) { return { a_v.x, a_v.y, a_v.z }; }
		RE::NiPoint3 ToNi(const Vec3& a_v) { return { a_v.x, a_v.y, a_v.z }; }

		struct Slot
		{
			sidecar::Sidecar data;
			SlotView         view;
			RE::TESRace*     race{ nullptr };
			RE::TESNPC*      npc{ nullptr };
			bool             placeholder{ false };
			RE::NiPoint3     pos;
			float            heading{ 0.0f };
			bool             finished{ false };  // ready or failed
			poses::Pose                rest{ "stand" };
			std::optional<poses::Pose> select;
			RE::ObjectRefHandle        furniture;
			RE::ObjectRefHandle        placedFurniture;  // spawned for a place: value (lobby game only)
			bool                       edited{ false };  // moved in the editor panel since the last save
			std::uint32_t              reseat{ 0 };      // editor: get back into moved furniture once moving stops
			// Editor choices not saved yet, layered over the layout files.
			std::optional<std::string> editedPose;
			std::optional<std::string> editedSelect;     // "none": stays in the rest pose
			std::optional<std::string> editedFurniture;  // a layout::FormatRef value, or "none"
		};

		struct Stage
		{
			RE::NiPoint3 centre;  // where the characters gather: the saved stage, or the middle of the default row
			RE::NiPoint3 camera;
			float        cameraYaw{ 0.0f };
			float        cameraPitch{ 0.0f };
			RE::NiPoint3 spawn;     // where coc put the player
			float        spawnYaw{ 0.0f };
		};

		std::mutex        g_lock;  // guards g_slots for GetSlots() from the UI thread
		std::atomic<State> g_state{ State::kIdle };  // read from the input, UI and Scaleform threads
		std::vector<Slot> g_slots;
		Stage             g_stage;
		std::string       g_targetCell;
		bool              g_triedFallback{ false };
		bool              g_spawnScheduled{ false };
		std::atomic<bool> g_revealed{ false };
		bool              g_revealScheduled{ false };
		bool              g_hudHidden{ false };
		bool              g_savingBlocked{ false };  // we set the player's disable-saving flag
		std::atomic<bool> g_editorOpen{ false };  // the layout editor panel (F8) is up
		bool              g_cameraEdited{ false };   // moved in the editor panel since the last save
		bool              g_weatherForced{ false };  // the layout's weather is forced in the lobby game

		// Warm light that follows the selected character. Its base form is
		// created at runtime (no plugin needed) in the lobby's game. We never free
		// runtime forms (slot NPC bases, this light, the poses' package); we
		// drop our pointers when that game ends.
		RE::TESObjectLIGH*  g_lightBase{ nullptr };
		RE::ObjectRefHandle g_light;
		std::size_t         g_highlighted{ 0 };
		std::uint32_t     g_enterGeneration{ 0 };  // invalidates stale delayed callbacks

		std::function<void(std::string_view)> g_onFailure;
		std::function<void()>                 g_onReady;

		// Every form the lobby created at runtime (NoteRuntimeForm). The
		// pointer is only compared, never dereferenced: it may be gone.
		struct RuntimeForm
		{
			RE::FormID         id{ 0 };
			const RE::TESForm* form{ nullptr };
			RE::FormType       type{ RE::FormType::None };
			std::string        kind;
		};
		std::vector<RuntimeForm> g_runtimeForms;

		// For each kind: how many of our forms are still registered under their
		// ID, how many IDs are free, and how many now belong to another form.
		void ReportRuntimeForms(std::string_view a_when, bool a_forget)
		{
			if (g_runtimeForms.empty()) {
				return;
			}
			struct Count
			{
				std::size_t kept{ 0 }, gone{ 0 }, reused{ 0 };
			};
			std::map<std::string, Count> counts;
			for (const auto& f : g_runtimeForms) {
				auto& c = counts[f.kind];
				const auto* now = RE::TESForm::LookupByID(f.id);
				if (!now) {
					++c.gone;
				} else if (now == f.form && now->GetFormType() == f.type) {
					++c.kept;
				} else {
					++c.reused;
				}
			}
			logger::info("Lobby: runtime forms after {} ({} created by the lobby):", a_when, g_runtimeForms.size());
			for (const auto& [kind, c] : counts) {
				logger::info("Lobby:   {}: {} still registered, {} no longer registered, {} IDs now used by another form", kind, c.kept, c.gone,
					c.reused);
			}
			if (a_forget) {
				g_runtimeForms.clear();
			}
		}

		void  TrySpawn(std::uint32_t a_generation, int a_attempt);
		float WrapAngle(float a_rad);

		void SetState(State a_state)
		{
			if (g_state != a_state) {
				logger::info("Lobby: state {} -> {}", StateName(g_state), StateName(a_state));
				g_state = a_state;
			}
		}

		bool RunConsoleCommand(std::string_view a_command)
		{
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
			auto* script = factory ? factory->Create() : nullptr;
			if (!script) {
				logger::error("Lobby: could not create a Script form for console command '{}'", a_command);
				return false;
			}
			script->SetCommand(a_command);
			script->CompileAndRun(nullptr);
			delete script;
			logger::info("Lobby: ran console command '{}'", a_command);
			return true;
		}

		// The lobby is a throwaway game; a save of it (quicksave, autosave, a
		// mod's save) would be junk full of our runtime forms. This is the flag
		// Game.SetInChargen(abDisableSaving) sets, written directly so it is
		// cleared synchronously before any real save loads.
		// The player is the lobby's camera, so its gameplay controls are off
		// while the lobby is up: Ctrl sneaked and right-click drew a weapon
		// (0.5.19). The game menu and the console stay on. Same lifecycle as
		// BlockSaving: back on before any load and on leaving the lobby.
		constexpr auto kLobbyControls = static_cast<RE::UserEvents::USER_EVENT_FLAG>(
			std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kMovement) | std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kLooking) |
			std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kActivate) | std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kPOVSwitch) |
			std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kFighting) | std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kSneaking) |
			std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kMainFour) | std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kWheelZoom) |
			std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kJumping) | std::to_underlying(RE::UserEvents::USER_EVENT_FLAG::kVATS));
		bool g_controlsBlocked{ false };

		void BlockControls(bool a_block)
		{
			auto* controls = RE::ControlMap::GetSingleton();
			if (!controls || a_block == g_controlsBlocked) {
				return;
			}
			controls->ToggleControls(kLobbyControls, !a_block, false);
			g_controlsBlocked = a_block;
			logger::info("Lobby: player controls {}", a_block ? "disabled" : "enabled again");
		}

		void BlockSaving(bool a_block)
		{
			BlockControls(a_block);
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			auto& flags = player->GetGameStatsData().byCharGenFlag;
			if (a_block) {
				// Set again even if we set it before: the new game the lobby
				// starts may have reset it.
				flags.set(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);
			} else if (g_savingBlocked) {
				flags.reset(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);
			}
			if (a_block != g_savingBlocked) {
				g_savingBlocked = a_block;
				logger::info("Lobby: saving {}", a_block ? "disabled" : "enabled again");
			}
		}

		void Fail(std::string_view a_reason)
		{
			logger::error("Lobby: failed: {}", a_reason);
			// Saving stays off: the lobby game may still be running. The main
			// menu (where a failure leads) turns it back on.
			SetState(State::kIdle);
			if (g_onFailure) {
				// The technical reason is in the log; players get a plain message.
				g_onFailure("Could not load the character select screen. See CharSelect.log for details.");
			}
		}

		void SetHudVisible(bool a_visible)
		{
			auto* ui = RE::UI::GetSingleton();
			auto  hud = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
			if (hud && hud->uiMovie) {
				hud->uiMovie->SetVisible(a_visible);
				g_hudHidden = !a_visible;
				logger::debug("Lobby: HUD {}", a_visible ? "shown" : "hidden");
			}
		}

		void LogPlayer(std::string_view a_when)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			const auto pos = player->GetPosition();
			auto*      cell = player->GetParentCell();
			const char* cellId = cell ? cell->GetFormEditorID() : nullptr;
			logger::info("Lobby: player {}: pos ({:.0f}, {:.0f}, {:.0f}) heading {:.2f} cell {:08X} '{}' attached={} 3D={}",
				a_when, pos.x, pos.y, pos.z, player->GetAngleZ(), cell ? cell->GetFormID() : 0, cellId ? cellId : "",
				cell && cell->IsAttached(), player->Is3DLoaded());
		}

		void ReportSlots(std::string_view a_when)
		{
			if (g_state != State::kActive) {
				return;
			}
			LogPlayer(a_when);
			auto* player = RE::PlayerCharacter::GetSingleton();
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				const auto& slot = g_slots[i];
				auto ref = slot.view.ref.get();
				if (!ref) {
					logger::info("Lobby:   slot {}: no reference", i + 1);
					continue;
				}
				const auto  pos = ref->GetPosition();
				auto*       actor = ref->As<RE::Actor>();
				auto*       npc = actor ? actor->GetActorBase() : nullptr;
				const auto* body = actor ? actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody) : nullptr;
				logger::info("Lobby:   slot {} {:08X}: pos ({:.0f}, {:.0f}, {:.0f}) off-target {:.0f} dist-to-camera {:.0f} disabled={} 3D={} AI={} ready={}",
					i + 1, ref->GetFormID(), pos.x, pos.y, pos.z, pos.GetDistance(slot.pos),
					player ? pos.GetDistance(player->GetPosition()) : -1.0f, ref->IsDisabled(), ref->Is3DLoaded(),
					actor && actor->IsAIEnabled(), slot.view.ready);
				logger::info("Lobby:     race {:08X} faceNPC {:08X} headParts {} weight {:.0f} body armor '{}'",
					actor && actor->GetRace() ? actor->GetRace()->GetFormID() : 0,
					npc && npc->faceNPC ? npc->faceNPC->GetFormID() : 0, npc ? npc->numHeadParts : 0, npc ? npc->weight : 0.0f,
					body && body->GetName() ? body->GetName() : "<none>");
			}
		}

		// What the hooded stand-in wears ([Lobby] sStandInOutfit).
		std::vector<sidecar::FormRef> StandInOutfit()
		{
			std::vector<sidecar::FormRef> out;
			for (const auto& text : config::Get().standInOutfit) {
				if (const auto key = ParseFormKey(text)) {
					out.push_back({ key->plugin, key->localId, "stand-in outfit" });
				} else {
					logger::warn("Lobby: stand-in outfit item '{}' is not 0x<form ID>|<plugin>", text);
				}
			}
			return out;
		}

		std::vector<Slot> CollectSlots()
		{
			std::vector<Slot> slots;
			const auto saveDir = paths::SaveDir();
			if (!saveDir) {
				return slots;
			}
			const auto scan = ScanSaveDirectory(*saveDir);
			logger::info("Lobby: {} characters in {}", scan.characters.size(), text::PathUtf8(*saveDir));

			auto*      dh = RE::TESDataHandler::GetSingleton();
			const auto max = config::Get().maxCharacters;
			for (const auto& character : scan.characters) {
				const auto  id = FormatCharacterId(character.characterId);
				const auto& latest = character.Latest();
				if (slots.size() >= max) {
					logger::info("Lobby:   {} '{}' - more than {} characters, not shown", id, latest.header.playerName, max);
					continue;
				}

				// Characters without a usable appearance still get a slot (no
				// actor, still playable) instead of silently disappearing.
				auto        data = sidecar::Read(character.characterId);
				RE::TESRace* race = nullptr;
				std::string  missing;
				if (!data) {
					missing = "no sidecar yet (load and save once to capture)";
				} else if (!std::filesystem::exists(paths::PresetPath(character.characterId))) {
					missing = std::format("no RaceMenu preset at {}", text::PathUtf8(paths::PresetPath(character.characterId)));
				} else if (race = dh ? dh->LookupForm<RE::TESRace>(data->race.localId, text::FromUtf8(data->race.plugin)) : nullptr; !race) {
					missing = std::format("race {}:{:08X} not loaded", data->race.plugin, data->race.localId);
				}

				Slot slot;
				slot.placeholder = !missing.empty();
				slot.view.placeholder = slot.placeholder;
				if (slot.placeholder) {
					logger::info("Lobby:   slot {}: {} '{}' lvl {} -> {} (placeholder: {})", slots.size() + 1, id, latest.header.playerName,
						latest.header.playerLevel, latest.stem, missing);
				} else {
					logger::info("Lobby:   slot {}: {} '{}' lvl {} -> {}", slots.size() + 1, id, data->name, latest.header.playerLevel, latest.stem);
				}

				slot.race = race;
				slot.view.name = latest.header.playerName;
				slot.view.level = latest.header.playerLevel;
				if (race && race->GetName() && *race->GetName()) {
					slot.view.race = text::ToUtf8(race->GetName());
				} else {
					// "NordRace" -> "Nord"
					slot.view.race = latest.header.raceEditorId;
					if (slot.view.race.ends_with("Race")) {
						slot.view.race.resize(slot.view.race.size() - 4);
					}
				}
				slot.view.location = latest.header.playerLocation;
				slot.view.lastPlayed = FormatFileTime(latest.header.fileTime);
				for (const auto& save : character.saves) {
					slot.view.saves.push_back(SaveEntry{
						text::FromUtf8(text::PathUtf8(save.path.filename())), save.stem, save.header.playerLevel, save.header.playerLocation,
						FormatFileTime(save.header.fileTime), save.name.saveType });
				}
				if (data) {
					slot.data = std::move(*data);
				}
				if (slot.placeholder) {
					// A hooded stand-in until the character is loaded or saved with the mod.
					slot.data.worn = StandInOutfit();
					slot.data.spells.clear();
				}
				slot.data.characterId = character.characterId;
				slot.data.name = latest.header.playerName;
				slots.push_back(std::move(slot));
			}
			return slots;
		}

		// A unique, throwaway actor base per slot, copied from the player's base
		// (always a valid humanoid) with everything that could leak the lobby
		// player's look or gear stripped out.
		RE::TESNPC* CreateSlotNpc(const Slot& a_slot)
		{
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESNPC>();
			auto* templ = RE::TESForm::LookupByID<RE::TESNPC>(kPlayerBaseId);
			auto* npc = factory ? factory->Create() : nullptr;
			if (!npc || !templ) {
				logger::error("Lobby: cannot create actor base (factory={}, template={})", static_cast<void*>(factory), static_cast<void*>(templ));
				return nullptr;
			}
			npc->Copy(templ);
			// Review L6 (unverified): heap members Copy() may leave shared with the
			// player's base. Head parts, face and head data are un-shared below;
			// this logs the rest so a session's log shows whether any are shared.
			logger::info("Lobby: base copy shares with the player's base: tint layers {}, perks {}, spells {}, factions {}",
				npc->tintLayers && npc->tintLayers == templ->tintLayers, npc->perks && npc->perks == templ->perks,
				npc->actorEffects && npc->actorEffects == templ->actorEffects,
				!npc->factions.empty() && npc->factions.data() == templ->factions.data());
			NoteRuntimeForm(npc, "actor base");
			logger::debug("Lobby: created actor base {:08X}; copied faceNPC={:08X} outfit={:08X} containerItems={}",
				npc->GetFormID(), npc->faceNPC ? npc->faceNPC->GetFormID() : 0,
				npc->defaultOutfit ? npc->defaultOutfit->GetFormID() : 0, npc->numContainerObjects);

			if (npc->GetFormID() == 0) {
				for (RE::FormID id = 0xFFFFF000; id < 0xFFFFFFF0; ++id) {
					if (!RE::TESForm::LookupByID(id)) {
						npc->SetFormID(id, false);
						break;
					}
				}
				logger::debug("Lobby: assigned formID {:08X}", npc->GetFormID());
			}

			// The face is generated from GetRootFaceNPC(); a copied faceNPC would
			// point back at the lobby player (a default Nord).
			npc->faceNPC = nullptr;
			// A stand-in keeps the lobby player's own race and sex: no preset
			// comes to rebuild its head, and another race's body under these
			// head parts would look broken.
			if (!a_slot.placeholder) {
				npc->race = a_slot.race;
				npc->originalRace = a_slot.race;
				if (a_slot.data.female) {
					npc->actorData.actorBaseFlags.set(RE::ACTOR_BASE_DATA::Flag::kFemale);
				} else {
					npc->actorData.actorBaseFlags.reset(RE::ACTOR_BASE_DATA::Flag::kFemale);
				}
			}
			npc->fullName = text::FromUtf8(a_slot.data.name);  // ours is UTF-8; the engine's is the game's code page

			// Copy() can leave the package list sharing the player base's nodes;
			// adding to it or freeing this base would then touch the player's.
			// Start empty (Poses gives it its one package).
			logger::debug("Lobby: copied package list had {} entries, starting empty", npc->aiPackages.packages.size());
			std::construct_at(&npc->aiPackages.packages);

			// No starting outfit or inventory: those get equipped when the actor
			// loads and override the gear we put on.
			npc->defaultOutfit = nullptr;
			npc->sleepOutfit = nullptr;
			npc->containerObjects = nullptr;
			npc->numContainerObjects = 0;

			// Copy() may share these with the player's base. RaceMenu frees the
			// head part array and writes into face/head data, so this base gets
			// its own copies. They must never be null: placing the actor queues
			// its head build on a worker thread, which can run before RaceMenu's
			// preset and reads faceData->parts (0.2.1 crashed on exactly that).
			if (npc->headParts && npc->numHeadParts > 0) {
				const auto count = static_cast<std::size_t>(npc->numHeadParts);
				auto*      parts = RE::calloc<RE::BGSHeadPart*>(count);
				std::copy_n(npc->headParts, count, parts);
				npc->headParts = parts;
			}
			auto* face = RE::calloc<RE::TESNPC::FaceData>(1);
			if (templ->faceData) {
				*face = *templ->faceData;
			}
			npc->faceData = face;
			auto* head = RE::calloc<RE::TESNPC::HeadRelatedData>(1);
			if (templ->headRelatedData) {
				*head = *templ->headRelatedData;
			}
			npc->headRelatedData = head;
			return npc;
		}

		// The equip slot for a recorded hand; null (the engine's choice, the
		// right hand for a one-handed weapon) when none was recorded.
		const RE::BGSEquipSlot* HandSlot(std::string_view a_hand)
		{
			auto* objects = a_hand.empty() ? nullptr : RE::BGSDefaultObjectManager::GetSingleton();
			if (!objects) {
				return nullptr;
			}
#pragma push_macro("GetObject")
#undef GetObject  // windows.h: GetObjectA
			return objects->GetObject<RE::BGSEquipSlot>(a_hand == "left" ? RE::DEFAULT_OBJECTS::kLeftHandEquip : RE::DEFAULT_OBJECTS::kRightHandEquip);
#pragma pop_macro("GetObject")
		}

		std::size_t EquipWorn(RE::Actor* a_actor, const Slot& a_slot)
		{
			auto*       dh = RE::TESDataHandler::GetSingleton();
			auto*       equip = RE::ActorEquipManager::GetSingleton();
			std::size_t equipped = 0;
			for (const auto& item : a_slot.data.worn) {
				auto* form = dh ? dh->LookupForm(item.localId, text::FromUtf8(item.plugin)) : nullptr;
				auto* bound = form ? form->As<RE::TESBoundObject>() : nullptr;
				if (!bound) {
					logger::info("Lobby:     skip '{}' ({}:{:08X}) - plugin or form not loaded", item.name, item.plugin, item.localId);
					continue;
				}
				// One per entry: a pair of the same weapon is listed once per hand.
				a_actor->AddObjectToContainer(bound, nullptr, 1, nullptr);
				equip->EquipObject(a_actor, bound, nullptr, 1, HandSlot(item.hand), false, true, false, true);
				++equipped;
			}
			// Spells in hand show when the select pose draws weapons
			// (DrawWeaponMagicHands). Added to this lobby actor only.
			for (const auto& item : a_slot.data.spells) {
				auto* spell = dh ? dh->LookupForm<RE::SpellItem>(item.localId, text::FromUtf8(item.plugin)) : nullptr;
				if (!spell) {
					logger::info("Lobby:     skip spell '{}' ({}:{:08X}) - plugin or form not loaded", item.name, item.plugin, item.localId);
					continue;
				}
				a_actor->AddSpell(spell);
				equip->EquipSpell(a_actor, spell, HandSlot(item.hand));
				logger::info("Lobby:     spell '{}' in the {} hand", item.name, item.hand);
			}
			return equipped;
		}

		void PlaceSlot(RE::Actor* a_actor, const Slot& a_slot)
		{
			// Moving an actor in furniture leaves it sitting in mid-air. Anyone
			// else goes to its spot, seated slots included: moving a disabled
			// actor doesn't stick, and 0.3.4-0.4.2 skipped seated slots once
			// enabled, so they started at the camera and walked to the far seat.
			if (poses::InFurniture(a_actor)) {
				logger::debug("Lobby: {:08X} is in furniture, not moving it", a_actor->GetFormID());
				return;
			}
			a_actor->SetPosition(a_slot.pos, true);
			a_actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_slot.heading });
		}

		void PlacePlayer()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			player->SetPosition(g_stage.camera, true);
			player->SetAngle(RE::NiPoint3{ g_stage.cameraPitch, 0.0f, g_stage.cameraYaw });
			if (auto* camera = RE::PlayerCamera::GetSingleton()) {
				camera->ForceFirstPerson();
			}
		}

		RE::TESObjectLIGH* LightBase()
		{
			if (g_lightBase) {
				return g_lightBase;
			}
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESObjectLIGH>();
			auto* light = factory ? factory->Create() : nullptr;
			if (!light) {
				logger::error("Lobby: cannot create the highlight light form");
				return nullptr;
			}
			light->data.time = -1;  // never burns out
			light->data.radius = 240;
			light->data.color.red = 255;
			light->data.color.green = 218;
			light->data.color.blue = 170;
			light->data.color.alpha = 0;
			light->data.flags = RE::TES_LIGHT_FLAGS::kNone;  // plain omni, no shadows: cheap
			light->data.fallofExponent = 1.0f;
			light->data.fov = 90.0f;
			light->data.nearDistance = 5.0f;
			light->fade = 1.8f;  // intensity
			g_lightBase = light;
			NoteRuntimeForm(light, "light base");
			logger::info("Lobby: created highlight light form {:08X}", light->GetFormID());
			return light;
		}

		void RemoveLight()
		{
			if (auto ref = g_light.get()) {
				ref->Disable();
				ref->SetDelete(true);
			}
			g_light.reset();
		}

		// Moves the light to just in front of slot a_index, at chest height,
		// on the camera side so it lights the face.
		void PlaceLight(std::size_t a_index)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* base = LightBase();
			if (!player || !base || a_index >= g_slots.size()) {
				return;
			}
			RemoveLight();
			const auto&  slot = g_slots[a_index];
			RE::NiPoint3 toCamera = g_stage.camera - slot.pos;
			toCamera.z = 0.0f;
			const float len = std::sqrt(toCamera.x * toCamera.x + toCamera.y * toCamera.y);
			if (len > 1.0f) {
				toCamera = toCamera / len;
			}
			RE::NiPoint3 pos = slot.pos + toCamera * 70.0f;
			pos.z += 115.0f;

			auto ref = player->PlaceObjectAtMe(base, false);
			if (!ref) {
				logger::error("Lobby: could not place the highlight light");
				return;
			}
			// Load its 3D at the target spot rather than moving a live light.
			ref->Disable();
			ref->SetPosition(pos);
			ref->Enable(false);
			g_light = ref->CreateRefHandle();
			NoteRuntimeForm(ref.get(), "light reference");
			g_highlighted = a_index;
			logger::debug("Lobby: highlight light {:08X} on slot {} at ({:.0f}, {:.0f}, {:.0f})", ref->GetFormID(), a_index + 1, pos.x, pos.y, pos.z);
		}

		void Reveal(std::string_view a_why)
		{
			if (g_revealed || g_state != State::kActive) {
				return;
			}
			g_revealed = true;
			logger::info("Lobby: {}, showing the select menu", a_why);
			PlaceLight(0);
			poses::Select(0);
			if (g_onReady) {
				g_onReady();
			}
		}

		// The splash stays up until every character is in its pose (seated
		// characters seated), so nobody is seen in the reference pose or
		// walking to a bench.
		void MaybeReveal()
		{
			if (g_revealed || g_state != State::kActive || g_revealScheduled) {
				return;
			}
			const auto done = std::ranges::count_if(g_slots, [](const Slot& s) { return s.finished; });
			SelectMenu::SetSplashText(std::format("Loading characters  {} / {}", done, g_slots.size()));
			const bool allDone = static_cast<std::size_t>(done) == g_slots.size();
			if (!allDone) {
				return;
			}
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				if (g_slots[i].view.ready && !poses::IsSettled(i)) {
					const auto generation = g_enterGeneration;
					RunAfter(kReadyPollInterval, [generation]() {
						if (generation == g_enterGeneration) {
							MaybeReveal();
						}
					});
					return;
				}
			}
			g_revealScheduled = true;
			const auto generation = g_enterGeneration;
			RunAfter(kRevealSettle, [generation]() {
				if (generation == g_enterGeneration) {
					Reveal("all characters in place");
				}
			});
		}

		void FinishSlotFailed(std::size_t a_index, std::string_view a_why)
		{
			if (a_index < g_slots.size()) {
				g_slots[a_index].finished = true;
				logger::error("Lobby: slot {} '{}' failed: {}", a_index + 1, g_slots[a_index].data.name, a_why);
			}
			MaybeReveal();
		}

		void ConfigurePoses(std::size_t a_index)
		{
			const auto& slot = g_slots[a_index];
			poses::Configure(a_index, slot.view.ref, slot.furniture, slot.pos, slot.heading, slot.rest, slot.select);
		}

		void FreezeSlot(std::uint32_t a_generation, std::size_t a_index)
		{
			if (a_generation != g_enterGeneration || g_state != State::kActive || a_index >= g_slots.size()) {
				return;
			}
			auto& slot = g_slots[a_index];
			auto  ref = slot.view.ref.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				FinishSlotFailed(a_index, "actor vanished before freezing");
				return;
			}
			// AI comes back on here, held in place by a do-nothing package;
			// with AI off the animation graph never reaches an idle.
			PlaceSlot(actor, slot);
			ConfigurePoses(a_index);
			poses::Start(a_index);
			{
				std::scoped_lock lock(g_lock);
				slot.view.ready = true;
			}
			slot.finished = true;
			logger::info("Lobby: slot {} '{}' ready ({:08X})", a_index + 1, slot.data.name, actor->GetFormID());
			MaybeReveal();
		}

		void WaitFor3D(std::uint32_t a_generation, std::size_t a_index, int a_attempt)
		{
			if (a_generation != g_enterGeneration || g_state != State::kActive || a_index >= g_slots.size()) {
				return;
			}
			auto& slot = g_slots[a_index];
			auto  ref = slot.view.ref.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				FinishSlotFailed(a_index, "actor vanished while loading 3D");
				return;
			}
			if (!actor->Is3DLoaded()) {
				if (a_attempt >= kReadyPollMax) {
					FinishSlotFailed(a_index, "3D never loaded");
					return;
				}
				RunAfter(kReadyPollInterval, [a_generation, a_index, a_attempt]() { WaitFor3D(a_generation, a_index, a_attempt + 1); });
				return;
			}
			PlaceSlot(actor, slot);
			const auto equipped = EquipWorn(actor, slot);
			logger::info("Lobby: slot {} 3D loaded; equipped {}/{} worn items", a_index + 1, equipped, slot.data.worn.size());
			RunAfter(kFreezeDelay, [a_generation, a_index]() { FreezeSlot(a_generation, a_index); });
		}

		void OnPresetApplied(std::uint32_t a_generation, std::size_t a_index, bool a_ok)
		{
			if (a_generation != g_enterGeneration || g_state != State::kActive || a_index >= g_slots.size()) {
				return;
			}
			auto& slot = g_slots[a_index];
			auto  ref = slot.view.ref.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				FinishSlotFailed(a_index, "actor vanished before the preset finished");
				return;
			}
			if (!a_ok) {
				// Leave it disabled: a base without face data is not safe to render.
				FinishSlotFailed(a_index, "CharGen.LoadCharacterEx failed, actor stays hidden");
				return;
			}
			actor->Enable(false);
			// AI on from the first frame, so its idle is running long before
			// the menu shows (0.3.3: a second of reference pose after the
			// splash). Its base's own top package (poses::PrepareBase) keeps it
			// here or seats it; without one, the default sandbox walked it off.
			actor->EnableAI(true);
			PlaceSlot(actor, slot);
			actor->SetActivationBlocked(true);
			WaitFor3D(a_generation, a_index, 0);
		}

		void SpawnSlot(std::size_t a_index, RE::PlayerCharacter* a_player);

		constexpr int kFurniture3DPollMax = 12;  // 3 s

		// Furniture placed for the lobby loads its 3D a moment after it is
		// enabled; spawn its character once it has, so the engine can put the
		// character straight into it (0.3.6: the bedroll wasn't loaded yet,
		// so the sleeper had to walk over, and got stuck on the bench).
		void SpawnSlotWhenFurnitureReady(std::uint32_t a_generation, std::size_t a_index, int a_attempt)
		{
			if (a_generation != g_enterGeneration || g_state != State::kActive || a_index >= g_slots.size()) {
				return;
			}
			auto  furniture = g_slots[a_index].placedFurniture.get();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (furniture && !furniture->Is3DLoaded() && a_attempt < kFurniture3DPollMax) {
				RunAfter(kReadyPollInterval, [a_generation, a_index, a_attempt]() { SpawnSlotWhenFurnitureReady(a_generation, a_index, a_attempt + 1); });
				return;
			}
			if (furniture) {
				logger::info("Lobby: slot {} furniture {:08X} 3D {} after {} ms", a_index + 1, furniture->GetFormID(),
					furniture->Is3DLoaded() ? "loaded" : "still not loaded", a_attempt * kReadyPollInterval.count());
			}
			if (player) {
				SpawnSlot(a_index, player);
			}
		}

		void SpawnSlot(std::size_t a_index, RE::PlayerCharacter* a_player)
		{
			auto& slot = g_slots[a_index];
			slot.npc = CreateSlotNpc(slot);
			if (!slot.npc) {
				FinishSlotFailed(a_index, "could not create actor base");
				return;
			}
			// Its own top package before it exists, so the first package
			// evaluation already keeps it here (or seats it in its furniture).
			poses::PrepareBase(slot.npc, slot.rest, static_cast<bool>(slot.furniture));

			auto  ref = a_player->PlaceObjectAtMe(slot.npc, false);
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				FinishSlotFailed(a_index, "PlaceObjectAtMe failed");
				return;
			}
			NoteRuntimeForm(actor, "actor");
			// Hidden until the preset is applied, so the head is built from it.
			actor->Disable();
			auto* excluded = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESFaction>(kBardAudienceExcludedFaction, "Skyrim.esm");
			if (excluded) {
				actor->AddToFaction(excluded, 0);
			} else {
				logger::warn("Lobby: BardAudienceExcludedFaction not found; a bard's song may pull slot {} away to applaud", a_index + 1);
			}
			poses::PrepareRef(actor, slot.furniture.get().get());
			PlaceSlot(actor, slot);
			{
				std::scoped_lock lock(g_lock);
				slot.view.ref = actor->CreateRefHandle();
			}
			logger::info("Lobby: slot {} '{}' placed as {:08X} (base {:08X}) at ({:.0f}, {:.0f}, {:.0f}){}", a_index + 1, slot.data.name,
				actor->GetFormID(), slot.npc->GetFormID(), slot.pos.x, slot.pos.y, slot.pos.z, slot.placeholder ? " as a hooded stand-in" : "");

			const auto generation = g_enterGeneration;
			if (slot.placeholder) {
				// No preset to apply: the copied base already has a whole head.
				// Enabled a moment later, not now: enabling in the same frame as
				// the Disable() above left it disabled for good (0.5.9).
				RunAfter(kReadyPollInterval, [generation, a_index]() { OnPresetApplied(generation, a_index, true); });
				return;
			}

			// LoadCharacterEx points the face tint at the exported DDS; make sure
			// something is there so a failed export can't render pink.
			tint::EnsurePlaceholder(slot.data.characterId);

			const bool queued = papyrus::CallStatic("CharGen", "LoadCharacterEx",
				[generation, a_index](const RE::BSScript::Variable& a_result) {
					const bool ok = a_result.IsBool() && a_result.GetBool();
					logger::info("Lobby: CharGen.LoadCharacterEx for slot {} returned {}", a_index + 1, ok);
					QueueTask([generation, a_index, ok]() { OnPresetApplied(generation, a_index, ok); });
				},
				static_cast<RE::Actor*>(actor), slot.race, RE::BSFixedString(slot.data.preset), kPresetApplyAll);
			if (!queued) {
				FinishSlotFailed(a_index, "could not call CharGen.LoadCharacterEx (RaceMenu missing?)");
			}
		}

		bool IsSlotActor(const RE::TESObjectREFR* a_ref)
		{
			return std::ranges::any_of(g_slots, [a_ref](const Slot& s) { return s.view.ref.get().get() == a_ref; });
		}

		// Hides the cell's own NPCs. NPCs keep their schedules in the lobby
		// game, so this runs for as long as the lobby does: anyone walking in
		// later (Sigurd, 0.3.4) is hidden within a couple of seconds.
		// Outdoors the game keeps several cells loaded around the player, so
		// "the lobby" is everything within this distance instead of one cell.
		constexpr float kOutdoorRadius = 4000.0f;

		// Calls a_fn for every reference in the lobby: the player's cell
		// indoors, everything within kOutdoorRadius outdoors.
		void ForEachLobbyRef(const std::function<RE::BSContainer::ForEachResult(RE::TESObjectREFR*)>& a_fn)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			if (!cell) {
				return;
			}
			if (cell->IsInteriorCell()) {
				cell->ForEachReference(a_fn);
			} else if (auto* tes = RE::TES::GetSingleton()) {
				tes->ForEachReferenceInRange(player, kOutdoorRadius, a_fn);
			}
		}

		std::size_t HideOtherActors()
		{
			std::size_t hidden = 0;
			std::string names;
			ForEachLobbyRef([&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (actor && !actor->IsPlayerRef() && !actor->IsDisabled() && !IsSlotActor(actor)) {
					actor->Disable();
					const char* name = actor->GetDisplayFullName();
					names += std::format("{}{:08X} '{}'", hidden ? ", " : "", actor->GetFormID(), name ? name : "");
					++hidden;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (hidden > 0) {
				logger::info("Lobby: hid {} other actors: {}", hidden, names);
			}
			return hidden;
		}

		constexpr auto kKeepClearInterval = std::chrono::seconds(2);

		void KeepCellClear(std::uint32_t a_generation)
		{
			if (a_generation != g_enterGeneration || g_state != State::kActive) {
				return;
			}
			HideOtherActors();
			RunAfter(kKeepClearInterval, [a_generation]() { KeepCellClear(a_generation); });
		}


		// Layouts are stored under the location the lobby was started in
		// (sLobbyCell, or the fallback): outdoors the player can stand in a
		// neighbouring cell with another name, or none.
		const std::string& LayoutKey()
		{
			return g_targetCell;
		}

		std::string CellId(RE::TESObjectCELL* a_cell)
		{
			const char* id = a_cell ? a_cell->GetFormEditorID() : nullptr;
			return id ? id : "";
		}

		// Drops the slot actor's link to its furniture. A link must never
		// outlive its target: when the actor is destroyed, the engine reads the
		// target to unregister the link, and a freed target crashes the game
		// (0.5.37: a load from the Ragged Flagon freed the cell's chairs before
		// the seated characters). So: unlink before deleting furniture we
		// placed, and before the lobby's world goes away (UnlinkSeats).
		void Unlink(const Slot& a_slot)
		{
			auto  ref = a_slot.view.ref.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (actor && actor->extraList.GetLinkedRef(nullptr)) {
				actor->extraList.SetLinkedRef(nullptr, nullptr);
			}
		}

		// Furniture for a "place:" value: spawned at the slot's spot (its own
		// runtime reference in the throwaway lobby game), or moved there if it
		// already exists. Disable/move/enable reloads its 3D at the new spot.
		RE::ObjectRefHandle PlaceFurniture(Slot& a_slot, RE::TESFurniture* a_base)
		{
			auto existing = a_slot.placedFurniture.get();
			// Already there: reloading it would throw its sitter out (a re-layout
			// for another slot's pose change).
			if (existing && existing->GetBaseObject() == a_base && !existing->IsDisabled() && existing->GetPosition().GetDistance(a_slot.pos) < 1.0f &&
				std::abs(WrapAngle(existing->GetAngleZ() - a_slot.heading)) < 0.01f) {
				return a_slot.placedFurniture;
			}
			if (existing && existing->GetBaseObject() != a_base) {
				Unlink(a_slot);
				existing->Disable();
				existing->SetDelete(true);
				existing.reset();
			}
			if (!existing) {
				auto* player = RE::PlayerCharacter::GetSingleton();
				existing = player ? player->PlaceObjectAtMe(a_base, false) : nullptr;
				if (!existing) {
					logger::error("Lobby: could not place furniture {:08X}", a_base->GetFormID());
					return {};
				}
				NoteRuntimeForm(existing.get(), "placed furniture");
			}
			existing->Disable();
			existing->SetPosition(a_slot.pos);
			existing->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_slot.heading });
			existing->Enable(false);
			a_slot.placedFurniture = existing->CreateRefHandle();
			logger::info("Lobby: placed furniture {:08X} (base {:08X}) at ({:.0f}, {:.0f}, {:.0f}) heading {:.2f}", existing->GetFormID(),
				a_base->GetFormID(), a_slot.pos.x, a_slot.pos.y, a_slot.pos.z, a_slot.heading);
			return a_slot.placedFurniture;
		}

		using SeatSpot = geometry::Seat;

		// A spot just in front of one of a_furniture's sit markers, facing the
		// way a sitter faces, so the sit package seats its character on the
		// spot (0.5.3: a new seat across the room meant a walk over).
		// Prefers the seat nearest a_near if one is close, otherwise the one
		// nearest the stage; a_taken lists markers other slots already use.
		std::optional<SeatSpot> FindSeatSpot(RE::TESObjectREFR* a_furniture, const RE::NiPoint3& a_near, const std::vector<std::uint32_t>& a_taken)
		{
			auto* root = a_furniture ? a_furniture->Get3D() : nullptr;
			auto* node = root ? RE::BSFurnitureMarkerNode::FindBSFurnitureMarkerNode(root) : nullptr;
			if (!node) {
				return std::nullopt;
			}
			const float           baseHeading = a_furniture->GetAngleZ();
			std::vector<SeatSpot> seats;
			for (std::uint32_t i = 0; i < node->markers.size(); ++i) {
				const auto& marker = node->markers[i];
				if (!marker.animationType.all(RE::BSFurnitureMarker::AnimationType::kSit) || std::ranges::contains(a_taken, i)) {
					continue;
				}
				const auto  seat = root->world * marker.offset;
				const float heading = WrapAngle(baseHeading + marker.heading);
				logger::debug("Lobby:   furniture {:08X} sit marker {} at ({:.0f}, {:.0f}, {:.0f}) heading {:.2f}", a_furniture->GetFormID(), i, seat.x,
					seat.y, seat.z, heading);
				seats.push_back({ geometry::SeatFront(ToVec(seat), heading, a_furniture->GetPositionZ()), heading, i });
			}
			return geometry::ChooseSeat(seats, ToVec(a_near), ToVec(g_stage.centre));
		}

		// Card order in the menu follows where characters stand, left to right.
		void UpdateSlotViews()
		{
			const RE::NiPoint3 right{ std::cos(g_stage.cameraYaw), -std::sin(g_stage.cameraYaw), 0.0f };
			std::scoped_lock   lock(g_lock);
			for (auto& slot : g_slots) {
				slot.view.lateral = (slot.pos.x - g_stage.camera.x) * right.x + (slot.pos.y - g_stage.camera.y) * right.y;
				slot.view.pos = slot.pos;
			}
		}

		// The cell's saved layout (shipped in CharSelectLayouts.ini, or saved
		// with the editor). Anything it doesn't place falls back to the
		// camera where the game put the player, and a row of characters in
		// front of it, to be arranged with the editor.
		void ComputeLayout(RE::TESObjectCELL* a_cell)
		{
			constexpr float kDefaultPitch = 0.12f;  // radians: looking slightly down
			const auto      saved = layout::Load(LayoutKey());

			if (g_cameraEdited) {
				// Moved in the editor panel and not saved yet: keep it.
			} else if (saved.camera) {
				g_stage.camera = *saved.camera;
				g_stage.cameraYaw = saved.cameraYaw;
				g_stage.cameraPitch = saved.cameraPitch;
				logger::info("Lobby: using saved camera ({:.0f}, {:.0f}, {:.0f}) yaw {:.2f} pitch {:.2f}",
					g_stage.camera.x, g_stage.camera.y, g_stage.camera.z, g_stage.cameraYaw, g_stage.cameraPitch);
			} else {
				g_stage.camera = g_stage.spawn;
				g_stage.cameraYaw = g_stage.spawnYaw;
				g_stage.cameraPitch = kDefaultPitch;
				logger::info("Lobby: no saved camera for '{}'; using where the player arrived", LayoutKey());
			}
			g_stage.centre = saved.stage ? *saved.stage : ToNi(geometry::RowCentre(ToVec(g_stage.camera), g_stage.cameraYaw));

			const auto n = g_slots.size();
			// Slots the layout doesn't place, in order: they make up the row.
			std::vector<std::size_t> unplaced;
			for (std::size_t i = 0; i < n; ++i) {
				if (!g_slots[i].edited && !(i < saved.slots.size() && saved.slots[i])) {
					unplaced.push_back(i);
				}
			}
			for (std::size_t i = 0; i < n; ++i) {
				auto& slot = g_slots[i];
				slot.rest = poses::Pose{ "stand" };
				slot.select.reset();
				slot.furniture.reset();
				if (i < saved.poses.size()) {
					// Unsaved choices from the editor panel win.
					const auto& posing = saved.poses[i];
					if (auto pose = poses::Parse(slot.editedPose.value_or(posing.pose))) {
						slot.rest = std::move(*pose);
					}
					slot.select = poses::Parse(slot.editedSelect.value_or(posing.select));
					if (auto* ref = layout::ResolveRef(slot.editedFurniture.value_or(posing.furniture))) {
						slot.furniture = ref->CreateRefHandle();
					}
				}
				if (slot.edited) {
					continue;  // moved in the editor panel and not saved yet
				}
				if (i < saved.slots.size() && saved.slots[i]) {
					slot.pos = saved.slots[i]->pos;
					slot.heading = saved.slots[i]->heading;
					logger::info("Lobby: slot {} using saved spot ({:.0f}, {:.0f}, {:.0f}) heading {:.2f}", i + 1, slot.pos.x, slot.pos.y, slot.pos.z, slot.heading);
					continue;
				}
				const auto index = static_cast<std::size_t>(std::ranges::find(unplaced, i) - unplaced.begin());
				const auto spot = geometry::RowSpot(ToVec(g_stage.camera), g_stage.cameraYaw, index, unplaced.size());
				slot.pos = ToNi(spot.pos);
				// Outdoors the ground isn't flat: never below the terrain.
				float ground = 0.0f;
				if (a_cell && a_cell->IsExteriorCell() && RE::TES::GetSingleton() && RE::TES::GetSingleton()->GetLandHeight(slot.pos, ground)) {
					slot.pos.z = (std::max)(slot.pos.z, ground);
				}
				slot.heading = spot.heading;
				logger::info("Lobby: slot {} has no saved spot; standing in the row in front of the camera", i + 1);
			}
			// Furniture spawned for "place:" values, now that every slot has its spot.
			for (std::size_t i = 0; i < n && i < saved.poses.size(); ++i) {
				auto& slot = g_slots[i];
				// A pose with its own marker (sit_floor, kneel, lie_down, ...)
				// places it unless the slot names other furniture.
				RE::TESFurniture* base = layout::ResolvePlaceable(slot.editedFurniture.value_or(saved.poses[i].furniture));
				if (!base && !slot.furniture && slot.rest.sit && !slot.rest.marker.empty()) {
					base = layout::ResolvePlaceable("place:" + slot.rest.marker);
				}
				if (base) {
					slot.furniture = PlaceFurniture(slot, base);
				} else if (auto old = slot.placedFurniture.get()) {
					Unlink(slot);
					old->Disable();
					old->SetDelete(true);
					slot.placedFurniture.reset();
				}
			}
			// Characters on the cell's own furniture wait in front of their
			// seat, whatever spot was saved, so they sit straight down and the
			// light and name plate follow them there.
			// Characters already sitting there choose first, nearest where they
			// sit, so binding someone else to their bench doesn't take their seat.
			std::map<RE::FormID, std::vector<std::uint32_t>> takenSeats;
			std::vector<std::pair<std::size_t, RE::NiPoint3>> order;
			for (std::size_t i = 0; i < n; ++i) {
				auto ref = g_slots[i].view.ref.get();
				auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
				if (actor && poses::InFurniture(actor) && actor->GetOccupiedFurniture() == g_slots[i].furniture) {
					order.emplace_back(i, actor->GetPosition());
				}
			}
			for (std::size_t i = 0; i < n; ++i) {
				if (!std::ranges::contains(order, i, &std::pair<std::size_t, RE::NiPoint3>::first)) {
					order.emplace_back(i, g_slots[i].pos);
				}
			}
			for (const auto& [i, from] : order) {
				auto&      slot = g_slots[i];
				auto       furniture = slot.furniture.get();
				const bool sits = slot.rest.sit || (slot.select && slot.select->sit);
				if (!furniture || slot.placedFurniture || !sits || slot.rest.sleep) {
					continue;
				}
				auto& taken = takenSeats[furniture->GetFormID()];
				if (const auto seat = FindSeatSpot(furniture.get(), from, taken)) {
					taken.push_back(seat->marker);
					slot.pos = ToNi(seat->pos);
					slot.heading = seat->heading;
					logger::info("Lobby: slot {} waits at seat {} of {:08X}: ({:.0f}, {:.0f}, {:.0f}) heading {:.2f}", i + 1, seat->marker,
						furniture->GetFormID(), slot.pos.x, slot.pos.y, slot.pos.z, slot.heading);
				} else {
					logger::warn("Lobby: slot {} furniture {:08X} has no free sit marker (3D loaded: {}); keeping its spot", i + 1,
						furniture->GetFormID(), furniture->Is3DLoaded());
				}
			}
			UpdateSlotViews();
			logger::info("Lobby: layout for '{}': stage ({:.0f}, {:.0f}, {:.0f}) camera ({:.0f}, {:.0f}, {:.0f}) yaw {:.2f} pitch {:.2f}, {} slots ({} in the default row)",
				LayoutKey(), g_stage.centre.x, g_stage.centre.y, g_stage.centre.z, g_stage.camera.x, g_stage.camera.y, g_stage.camera.z,
				g_stage.cameraYaw, g_stage.cameraPitch, n, unplaced.size());
		}

		bool CellMatches(RE::TESObjectCELL* a_cell, std::string_view a_editorId)
		{
			const char* id = a_cell ? a_cell->GetFormEditorID() : nullptr;
			return id && _stricmp(id, std::string(a_editorId).c_str()) == 0;
		}

		void IssueCoc(std::string_view a_cell)
		{
			g_targetCell = std::string(a_cell);
			g_spawnScheduled = false;
			if (!RunConsoleCommand(std::format("coc {}", a_cell))) {
				Fail("console command failed");
			}
		}

		void TryFallbackOrFail(std::string_view a_reason)
		{
			const auto& fallback = config::Get().fallbackCell;
			if (!g_triedFallback && !fallback.empty() && _stricmp(fallback.c_str(), g_targetCell.c_str()) != 0) {
				logger::warn("Lobby: {} - trying fallback cell '{}'", a_reason, fallback);
				g_triedFallback = true;
				IssueCoc(fallback);
				return;
			}
			Fail(a_reason);
		}

		// The layout's time of day and weather, if it sets them. Both belong to
		// the lobby game: a loaded save brings its own, and a forced weather is
		// released before any load (Teardown).
		void ApplyTimeAndWeather()
		{
			const auto saved = layout::Load(LayoutKey());
			if (saved.hour) {
				if (auto* calendar = RE::Calendar::GetSingleton(); calendar && calendar->gameHour) {
					calendar->gameHour->value = std::clamp(*saved.hour, 0.0f, 23.99f);
					logger::info("Lobby: time of day set to {:.2f}", calendar->gameHour->value);
				}
			}
			if (!saved.weather.empty()) {
				const auto key = ParseFormKey(saved.weather);
				auto*      dh = RE::TESDataHandler::GetSingleton();
				auto*      weather = key && dh ? dh->LookupForm<RE::TESWeather>(key->localId, text::FromUtf8(key->plugin)) : nullptr;
				auto*      sky = RE::Sky::GetSingleton();
				if (weather && sky) {
					sky->ForceWeather(weather, true);
					g_weatherForced = true;
					logger::info("Lobby: weather set to {:08X}", weather->GetFormID());
				} else {
					logger::warn("Lobby: weather '{}' is not a loaded weather; keeping the game's", saved.weather);
				}
			}
		}

		void StageLobby(RE::PlayerCharacter* a_player)
		{
			auto* cell = a_player->GetParentCell();
			SetState(State::kActive);
			BlockSaving(true);
			LogPlayer("at staging");
			HideOtherActors();
			ApplyTimeAndWeather();
			SetHudVisible(false);
			g_stage.spawn = a_player->GetPosition();
			g_stage.spawnYaw = a_player->GetAngleZ();
			ComputeLayout(cell);
			PlacePlayer();
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				if (g_slots[i].placedFurniture) {
					SpawnSlotWhenFurnitureReady(g_enterGeneration, i, 0);
				} else {
					SpawnSlot(i, a_player);
				}
			}
			const auto generation = g_enterGeneration;
			RunAfter(kRevealTimeout, [generation]() {
				if (generation == g_enterGeneration && !g_revealed && g_state == State::kActive) {
					logger::warn("Lobby: reveal timeout, showing the menu with unfinished slots");
					for (auto& s : g_slots) {
						s.finished = true;
					}
					Reveal("reveal timeout");
				}
			});
			RunAfter(kKeepClearInterval, [generation]() { KeepCellClear(generation); });
			RunAfter(std::chrono::seconds(5), [generation]() {
				if (generation == g_enterGeneration && g_state == State::kActive) {
					ReportSlots("5s after staging");
					poses::Report("5s after staging");
				}
			});
		}

		void TrySpawn(std::uint32_t a_generation, int a_attempt)
		{
			if (a_generation != g_enterGeneration || g_state != State::kEntering) {
				return;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			// Outdoors the player can arrive in a neighbouring cell (or one
			// without an editor ID), so any outdoor cell counts.
			const bool inTarget = cell && (CellMatches(cell, g_targetCell) || cell->IsExteriorCell());
			if (!(player && inTarget && cell->IsAttached() && player->Is3DLoaded())) {
				if (a_attempt >= kReadyPollMax) {
					LogPlayer("gave up waiting");
					TryFallbackOrFail(std::format("player never settled in '{}'", g_targetCell));
					return;
				}
				if (a_attempt % 4 == 0) {
					LogPlayer(std::format("not ready (attempt {})", a_attempt));
				}
				RunAfter(kReadyPollInterval, [a_generation, a_attempt]() { TrySpawn(a_generation, a_attempt + 1); });
				return;
			}
			g_spawnScheduled = false;
			StageLobby(player);
		}

		// L1: notifications mods send while starting up in the lobby go to the
		// hidden HUD, where they normally play out unseen (its queue runs per
		// frame). Anything still queued when PLAY loads a save would show in
		// the real game, so clear it, and log how many there were.
		void FlushHudMessages()
		{
			QueueUITask([]() {
				auto*        ui = RE::UI::GetSingleton();
				auto         hud = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
				auto*        movie = hud ? hud->uiMovie.get() : nullptr;
				RE::GFxValue queue;
				const bool   found = movie && (movie->GetVariable(&queue, "_root.HUDMovieBaseInstance.MessagesInstance.MessageArray") ||
                                             movie->GetVariable(&queue, "_root.HUDMovieBaseInstance.MessagesBlock.MessagesInstance.MessageArray"));
				if (!found || !queue.IsArray()) {
					logger::info("Lobby: HUD notification queue not found; nothing cleared");
					return;
				}
				const auto queued = queue.GetArraySize();
				if (queued > 0) {
					queue.ClearElements();
				}
				logger::info("Lobby: {} HUD notifications from the lobby were still queued{}", queued, queued > 0 ? "; cleared" : "");
			});
		}

		constexpr auto kLoadStartTimeout = std::chrono::seconds(20);
		// When the last PLAY/LOAD from the lobby started; cleared once a load
		// succeeds. A main menu soon after means the load failed (loop breaker).
		rules::LoadFailureTracker g_loadAttempt;
		bool                      g_loadStarted{ false };  // PreLoadGame seen since LoadFile

		// The lobby is over (a load into a real game, the main menu, a load
		// while entering): forget it, and make every delayed callback stale.
		void ForgetLobby()
		{
			{
				std::scoped_lock lock(g_lock);
				g_slots.clear();
			}
			g_spawnScheduled = false;
			g_editorOpen = false;
			g_light.reset();
			g_lightBase = nullptr;
			g_highlighted = 0;
			poses::Reset(true);
			++g_enterGeneration;
			SetState(State::kIdle);
		}

		// Links seated characters to their furniture again, when the lobby
		// carries on after UnlinkSeats.
		void RelinkSeats()
		{
			for (const auto& slot : g_slots) {
				if (slot.view.ready) {
					auto ref = slot.view.ref.get();
					poses::PrepareRef(ref ? ref->As<RE::Actor>() : nullptr, slot.furniture.get().get());
				}
			}
		}

		// Back to a working lobby after a load that didn't happen: undoes what
		// LoadFile stopped (0.5.18 came back without poses, without the
		// NPC-clearing timer, and with the HUD showing).
		void ResumeLobby()
		{
			SetState(State::kActive);
			BlockSaving(true);
			SetHudVisible(false);
			RelinkSeats();  // unlinked for the load (LoadFile)
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				if (g_slots[i].view.ready) {
					ConfigurePoses(i);
					poses::Start(i);
				}
			}
			poses::Select(g_highlighted);
			PlaceLight(g_highlighted);
			const auto generation = g_enterGeneration;
			RunAfter(kKeepClearInterval, [generation]() { KeepCellClear(generation); });
			// The select screen again (out of the fade to black after PLAY); after
			// a LOAD, once the Journal closes (OnJournalClosed).
			auto* ui = RE::UI::GetSingleton();
			if (!(ui && ui->IsMenuOpen(RE::JournalMenu::MENU_NAME)) && g_onReady) {
				g_onReady();
			}
		}

		// Checks a PLAY load has started. A message box (a mod's, or any prompt
		// waiting on the player; PLAY itself skips the missing-content prompt)
		// or the loading screen being up means it's still under way: look again.
		void WatchLoadStart(std::uint32_t a_generation)
		{
			RunAfter(kLoadStartTimeout, [a_generation]() {
				auto*      ui = RE::UI::GetSingleton();
				const bool waiting = ui && (ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
				switch (rules::CheckLoadStart(a_generation == g_enterGeneration, g_state, g_loadStarted, waiting)) {
				case rules::LoadWatch::kStop:
					return;
				case rules::LoadWatch::kRecheck:
					WatchLoadStart(a_generation);
					return;
				case rules::LoadWatch::kResume:
					logger::error("Lobby: the load never started; back to the select screen");
					ResumeLobby();
					return;
				}
			});
		}

		void LoadFile(std::string a_file, std::string_view a_who)
		{
			if (!rules::CanStartLoad(g_state)) {
				return;
			}
			logger::info("Lobby: loading {} for '{}'", a_file, a_who);
			SetState(State::kLoadingSave);
			g_loadStarted = false;
			g_loadAttempt.Started(std::chrono::steady_clock::now());
			poses::Reset(false);  // a cancelled load resumes this lobby
			// Now, not at PreLoadGame: by then the characters' links can no
			// longer be found (0.5.38's log), though the world is still freed
			// after it. ResumeLobby links them again.
			UnlinkSeats();
			// Saving, controls and the HUD stay as they are until the load
			// actually starts (OnPreLoadGame): ClearPresets is a Papyrus round
			// trip that can take seconds on a busy VM, and an autosave in that
			// gap would save the lobby game.
			FlushHudMessages();

			// A load that never starts would leave the screen black (the select
			// menu stays up, black, until the loading screen): come back instead.
			WatchLoadStart(g_enterGeneration);

			auto load = [file = std::move(a_file), generation = g_enterGeneration]() {
				// ClearPresets is a Papyrus round trip: by now the watchdog may
				// have resumed the lobby, or another PLAY may have started (L2).
				if (generation != g_enterGeneration || g_state != State::kLoadingSave) {
					logger::info("Lobby: not loading {}: the lobby moved on while RaceMenu cleared its presets", file);
					return;
				}
				auto* manager = RE::BGSSaveLoadManager::GetSingleton();
				if (!manager) {
					logger::error("Lobby: BGSSaveLoadManager unavailable");
					return;
				}
				// CommonLib calls this flag a_checkForMods, and its one-argument
				// Load passes true, but what it does (seen in game, 0.5.21): true
				// loads without the missing-content prompt (a documented
				// limitation); false abandons the load and returns to the main
				// menu, which looped with the lobby re-entering. So it reads as
				// "mods already checked, go ahead": the game's own load menu
				// passes true after the player accepts its prompt.
				logger::info("Lobby: BGSSaveLoadManager::Load('{}', true)", file);
				manager->Load(file.c_str(), true);
			};
			ClearPresets(load);
		}
	}

	State GetState()
	{
		return g_state;
	}

	std::vector<SlotView> GetSlots()
	{
		std::scoped_lock      lock(g_lock);
		std::vector<SlotView> out;
		out.reserve(g_slots.size());
		for (const auto& s : g_slots) {
			out.push_back(s.view);
		}
		return out;
	}

	std::vector<std::pair<bool, RE::ObjectRefHandle>> GetSlotStatus()
	{
		std::scoped_lock                                  lock(g_lock);
		std::vector<std::pair<bool, RE::ObjectRefHandle>> out;
		out.reserve(g_slots.size());
		for (const auto& s : g_slots) {
			out.emplace_back(s.view.ready, s.view.ref);
		}
		return out;
	}

	void SetFailureHandler(std::function<void(std::string_view)> a_handler)
	{
		g_onFailure = std::move(a_handler);
	}

	void SetReadyHandler(std::function<void()> a_handler)
	{
		g_onReady = std::move(a_handler);
	}

	void RequestEnter()
	{
		if (!rules::CanEnter(g_state)) {
			logger::info("Lobby: enter requested but state is {}", StateName(g_state));
			return;
		}
		logger::info("Lobby: enter requested");

		auto slots = CollectSlots();
		if (slots.empty()) {
			if (g_onFailure) {
				g_onFailure("No saved characters found.");
			}
			return;
		}
		{
			std::scoped_lock lock(g_lock);
			g_slots = std::move(slots);
		}
		++g_enterGeneration;
		g_triedFallback = false;
		g_revealed = false;
		g_revealScheduled = false;
		g_editorOpen = false;
		// Forms from a previous lobby game may be gone (e.g. after Back to main menu).
		g_light.reset();
		g_lightBase = nullptr;
		g_highlighted = 0;
		g_stage = {};
		poses::Reset(true);

		SetState(State::kEntering);
		IssueCoc(config::Get().lobbyCell);

		const auto generation = g_enterGeneration;
		RunAfter(kEnterWatchdog, [generation]() {
			if (generation == g_enterGeneration && g_state == State::kEntering && !g_spawnScheduled) {
				LogPlayer("watchdog");
				TryFallbackOrFail("no loading screen after coc (bad cell editor ID?)");
			}
		});
	}

	void OnLoadingMenuClosed()
	{
		if (g_state != State::kEntering) {
			return;
		}
		LogPlayer("at loading screen close");
		// The lobby game is running: no saving from here on.
		BlockSaving(true);
		// The engine closes our menu when the loading screen opens; cover the
		// scene again until everyone is in place (0.3.4 showed actors popping
		// in and getting dressed for ~3.7 s).
		if (!SelectMenu::IsOpen()) {
			SelectMenu::ShowSplash("Loading characters");
		}
		if (g_spawnScheduled) {
			return;
		}
		g_spawnScheduled = true;
		const auto generation = g_enterGeneration;
		RunAfter(kSettleDelay, [generation]() { TrySpawn(generation, 0); });
	}

	void Play(std::size_t a_slot)
	{
		if (!rules::CanPlay(g_state, a_slot, g_slots.size(), a_slot < g_slots.size() && !g_slots[a_slot].view.saves.empty())) {
			logger::info("Lobby: Play on invalid slot {} (state {})", a_slot + 1, StateName(g_state));
			if (g_state == State::kActive) {
				SelectMenu::ShowSelect();  // back from the fade to black
			}
			return;
		}
		LoadFile(g_slots[a_slot].view.saves.front().file, g_slots[a_slot].data.name);
	}

	namespace
	{
		std::size_t             g_editReturnHighlight{ 0 };
		std::vector<EditTarget> g_editTargets;  // snapshot for the menu (UI thread), under g_lock

		constexpr auto kReseatDelay = std::chrono::milliseconds(700);

		float WrapAngle(float a_rad)
		{
			return geometry::WrapAngle(a_rad);
		}

		int Degrees(float a_rad)
		{
			return geometry::Degrees(a_rad);
		}

		std::string KindOf(const Slot& a_slot)
		{
			if (a_slot.placeholder) {
				return "placeholder";
			}
			if (a_slot.rest.sit && a_slot.furniture && a_slot.rest.marker.empty()) {
				return a_slot.rest.sleep ? "asleep" : "seated";
			}
			std::string kind = a_slot.rest.name == "stand" ? "standing" : a_slot.rest.name;
			std::ranges::replace(kind, '_', ' ');
			return kind;
		}

		std::string PoseLabel(std::string a_name)
		{
			std::ranges::replace(a_name, '_', ' ');
			return a_name;
		}

		std::string SeatLabel(const Slot& a_slot)
		{
			auto furniture = a_slot.furniture.get();
			if (!furniture) {
				return "none";
			}
			if (a_slot.placedFurniture) {
				return a_slot.rest.marker.empty() ? "placed furniture" : "pose marker";
			}
			const char* name = furniture->GetDisplayFullName();
			return name && *name ? text::ToUtf8(name) : std::format("furniture {:08X}", furniture->GetFormID());
		}

		// The cell's own seats (or beds) the editor's seat picker cycles
		// through, nearest the stage first. Not furniture placed for the lobby,
		// which can't be saved.
		std::vector<RE::TESObjectREFR*> SeatsInCell(bool a_beds)
		{
			std::vector<RE::TESObjectREFR*> seats;
			const auto wanted = a_beds ? RE::BSFurnitureMarker::AnimationType::kSleep : RE::BSFurnitureMarker::AnimationType::kSit;
			ForEachLobbyRef([&](RE::TESObjectREFR* a_ref) {
				auto* base = a_ref ? a_ref->GetBaseObject() : nullptr;
				if (!base || !base->Is(RE::FormType::Furniture) || a_ref->IsDisabled() || a_ref->IsDynamicForm()) {
					return RE::BSContainer::ForEachResult::kContinue;
				}
				auto* root = a_ref->Get3D();
				auto* node = root ? RE::BSFurnitureMarkerNode::FindBSFurnitureMarkerNode(root) : nullptr;
				if (node && std::ranges::any_of(node->markers, [&](const RE::BSFurnitureMarker& m) { return m.animationType.any(wanted); })) {
					seats.push_back(a_ref);
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			std::ranges::sort(seats, {}, [](RE::TESObjectREFR* a_ref) { return a_ref->GetPosition().GetDistance(g_stage.centre); });
			return seats;
		}

		void RefreshEditTargets()
		{
			std::vector<EditTarget> targets;
			EditTarget              camera;
			camera.label = "Camera";
			camera.camera = true;
			camera.detail = std::format("x {:.0f}  y {:.0f}  z {:.0f}{}turn {}\xC2\xB0{}tilt {}\xC2\xB0", g_stage.camera.x, g_stage.camera.y,
				g_stage.camera.z, "   ", Degrees(g_stage.cameraYaw), "   ", static_cast<int>(std::lround(g_stage.cameraPitch * 180.0f / RE::NI_PI)));
			targets.push_back(std::move(camera));
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				const auto& slot = g_slots[i];
				EditTarget  t;
				t.label = std::format("{}  {}", i + 1, slot.view.name);
				t.kind = KindOf(slot);
				t.detail = std::format("x {:.0f}  y {:.0f}  z {:.0f}   facing {}\xC2\xB0", slot.pos.x, slot.pos.y, slot.pos.z, Degrees(slot.heading));
				const bool sits = slot.rest.sit || (slot.select && slot.select->sit);
				if (slot.furniture && !slot.placedFurniture && sits) {
					t.movable = false;
					t.note = "Uses a seat.";
				}
				t.rest = PoseLabel(slot.rest.name);
				t.select = slot.select ? PoseLabel(slot.select->name) : "none";
				t.seat = SeatLabel(slot);
				const auto needsSeat = [&](const poses::Pose& a_pose) { return a_pose.sit && a_pose.marker.empty() && !slot.furniture; };
				if (needsSeat(slot.rest) || (slot.select && needsSeat(*slot.select))) {
					t.poseNote = "This pose needs a seat.";
				}
				targets.push_back(std::move(t));
			}
			std::scoped_lock lock(g_lock);
			g_editTargets = std::move(targets);
		}

		// Puts slot a_index where its (edited) spot says, live.
		void MoveSlotNow(std::size_t a_index)
		{
			auto& slot = g_slots[a_index];
			auto  ref = slot.view.ref.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (auto furniture = slot.placedFurniture.get()) {
				// Furniture placed for the lobby moves with its slot; its
				// character gets out, moves along, and gets back in once the
				// moving stops.
				auto* base = furniture->GetBaseObject();
				if (auto* furnitureBase = base ? base->As<RE::TESFurniture>() : nullptr) {
					if (actor && poses::InFurniture(actor)) {
						actor->StopInteractingQuick(false);
					}
					slot.furniture = PlaceFurniture(slot, furnitureBase);
					if (actor) {
						actor->SetPosition(slot.pos, true);
						actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, slot.heading });
					}
					const auto token = ++slot.reseat;
					const auto generation = g_enterGeneration;
					RunAfter(kReseatDelay, [generation, a_index, token]() {
						if (generation == g_enterGeneration && g_state == State::kActive && a_index < g_slots.size() &&
							g_slots[a_index].reseat == token && g_slots[a_index].view.ready) {
							ConfigurePoses(a_index);
							poses::Reapply(a_index);
						}
					});
				}
			} else if (actor) {
				PlaceSlot(actor, slot);
			}
			if (slot.view.ready) {
				ConfigurePoses(a_index);  // new home for the drift guard
			}
			UpdateSlotViews();
			PlaceLight(a_index);
		}

		void SaveEdits()
		{
			const auto cell = LayoutKey();
			if (g_cameraEdited) {
				layout::SaveCamera(cell, g_stage.camera, g_stage.cameraYaw, g_stage.cameraPitch);
				logger::info("Lobby: editor saved the camera for '{}'", cell);
			}
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				auto& slot = g_slots[i];
				if (slot.edited && i < layout::kMaxSlots) {
					layout::SaveSlot(cell, i, slot.pos, slot.heading);
					logger::info("Lobby: editor saved spot {} for '{}' at ({:.1f}, {:.1f}, {:.1f}) heading {:.4f}", i + 1, cell, slot.pos.x,
						slot.pos.y, slot.pos.z, slot.heading);
				}
				const std::array<std::pair<const char*, std::optional<std::string>*>, 3> settings{ { { "pose", &slot.editedPose },
					{ "select", &slot.editedSelect }, { "furniture", &slot.editedFurniture } } };
				for (const auto& [key, value] : settings) {
					if (*value && i < layout::kMaxSlots) {
						layout::SaveSlotSetting(cell, i, key, **value);
						logger::info("Lobby: editor saved slot{}{} = {} for '{}'", i + 1, key, **value, cell);
					}
					value->reset();
				}
				slot.edited = false;
			}
			g_cameraEdited = false;
		}

		void ForgetEdits()
		{
			g_cameraEdited = false;
			for (auto& slot : g_slots) {
				slot.edited = false;
				slot.editedPose.reset();
				slot.editedSelect.reset();
				slot.editedFurniture.reset();
			}
		}

		// Puts one character where the current layout says and into its pose:
		// moved if standing, left alone if seated where it belongs, otherwise
		// taken out of its seat and moved to the new spot first.
		void ResettleSlot(std::size_t a_index)
		{
			auto& slot = g_slots[a_index];
			auto  ref = slot.view.ref.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				return;
			}
			if (slot.view.ready) {
				ConfigurePoses(a_index);
			}
			const bool sits = slot.rest.sit || (slot.select && slot.select->sit);
			if (!poses::IsSeated(a_index)) {
				PlaceSlot(actor, slot);
				poses::Reapply(a_index);
			} else if (sits && slot.furniture && actor->GetOccupiedFurniture() == slot.furniture) {
				poses::Reapply(a_index);  // stays seated, or stands up if selected into a standing pose
			} else {
				// Seated somewhere it no longer belongs (another seat picked, or
				// a standing pose): out, over to the new spot, and into its pose
				// once out, rather than walking across the room.
				logger::info("Lobby: slot {} leaves its seat for ({:.0f}, {:.0f}, {:.0f})", a_index + 1, slot.pos.x, slot.pos.y, slot.pos.z);
				actor->StopInteractingQuick(false);
				actor->SetPosition(slot.pos, true);
				actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, slot.heading });
				const auto token = ++slot.reseat;
				const auto generation = g_enterGeneration;
				RunAfter(kReseatDelay, [generation, a_index, token]() {
					if (generation == g_enterGeneration && g_state == State::kActive && a_index < g_slots.size() && g_slots[a_index].reseat == token &&
						g_slots[a_index].view.ready) {
						poses::Reapply(a_index);
					}
				});
			}
		}
	}

	void Summon()
	{
		if (g_state != State::kActive) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		logger::info("Lobby: recomputing layout");
		ComputeLayout(player ? player->GetParentCell() : nullptr);
		PlacePlayer();
		for (std::size_t i = 0; i < g_slots.size(); ++i) {
			ResettleSlot(i);
		}
		PlaceLight(g_highlighted);
		if (g_editorOpen) {
			RefreshEditTargets();
		}
		ReportSlots("after re-layout");
	}

	bool IsEditorOpen()
	{
		return g_editorOpen;
	}

	std::vector<EditTarget> GetEditTargets()
	{
		std::scoped_lock lock(g_lock);
		return g_editTargets;
	}

	void OpenEditor()
	{
		if (g_state != State::kActive || g_editorOpen) {
			return;
		}
		logger::info("Lobby: editor panel open");
		g_editorOpen = true;
		g_editReturnHighlight = g_highlighted;
		ForgetEdits();
		RefreshEditTargets();
		SelectMenu::ShowEdit();
	}

	void CloseEditor(bool a_save)
	{
		if (!g_editorOpen) {
			return;
		}
		if (a_save) {
			SaveEdits();
		} else {
			logger::info("Lobby: editor cancelled, reverting to the saved layout");
			ForgetEdits();
			Summon();
		}
		g_editorOpen = false;
		logger::info("Lobby: editor panel closed");
		PlaceLight(g_editReturnHighlight);
		SelectMenu::ShowSelect();
	}

	void EditNudge(std::size_t a_target, float a_forward, float a_right, float a_up, float a_turn, float a_tilt)
	{
		if (!g_editorOpen || g_state != State::kActive) {
			return;
		}
		const float        yaw = g_stage.cameraYaw;
		const RE::NiPoint3 forward{ std::sin(yaw), std::cos(yaw), 0.0f };
		const RE::NiPoint3 right{ std::cos(yaw), -std::sin(yaw), 0.0f };
		const RE::NiPoint3 delta = forward * a_forward + right * a_right + RE::NiPoint3{ 0.0f, 0.0f, a_up };
		if (a_target == 0) {
			g_stage.camera += delta;
			g_stage.cameraYaw = WrapAngle(g_stage.cameraYaw + a_turn);
			g_stage.cameraPitch = std::clamp(g_stage.cameraPitch + a_tilt, -1.2f, 1.2f);
			g_cameraEdited = true;
			PlacePlayer();
			UpdateSlotViews();
		} else {
			const auto index = a_target - 1;
			if (index >= g_slots.size()) {
				return;
			}
			auto& slot = g_slots[index];
			if (slot.furniture && !slot.placedFurniture && (slot.rest.sit || (slot.select && slot.select->sit))) {
				return;  // waits at the cell's own seat; the seat picker chooses another
			}
			slot.pos += delta;
			slot.heading = WrapAngle(slot.heading + a_turn);
			slot.edited = true;
			MoveSlotNow(index);
		}
		RefreshEditTargets();
	}

	void EditFocus(std::size_t a_target)
	{
		if (g_editorOpen && a_target > 0 && a_target - 1 < g_slots.size()) {
			PlaceLight(a_target - 1);
		}
	}

	void EditCycle(std::size_t a_target, EditField a_field, int a_dir)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!g_editorOpen || g_state != State::kActive || !player || a_target == 0 || a_target > g_slots.size() || a_dir == 0) {
			return;
		}
		const auto index = a_target - 1;
		auto&      slot = g_slots[index];
		// a_pos -1: not in the list (a custom INI value); start at an end.
		const auto step = [a_dir](int a_pos, int a_count) { return geometry::CycleStep(a_pos, a_count, a_dir); };
		const char* field = "pose";
		std::string value;
		if (a_field == EditField::kSeat) {
			field = "furniture";
			// "none", then the cell's seats (beds for a sleeper), nearest the stage first.
			const auto seats = SeatsInCell(slot.rest.sleep);
			auto       current = slot.placedFurniture ? nullptr : slot.furniture.get().get();
			const auto it = std::ranges::find(seats, current);
			const int  pos = current ? (it == seats.end() ? -1 : static_cast<int>(it - seats.begin()) + 1) : 0;
			const int  next = step(pos, static_cast<int>(seats.size()) + 1);
			value = next == 0 ? "none" : layout::FormatRef(seats[next - 1]);
			if (value.empty()) {
				return;
			}
			slot.editedFurniture = value;
		} else {
			const bool  select = a_field == EditField::kSelect;
			const auto& list = poses::PickList(select);
			const auto  current = select ? (slot.select ? slot.select->name : std::string("none")) : slot.rest.name;
			const auto  it = std::ranges::find(list, current);
			const int   pos = it == list.end() ? -1 : static_cast<int>(it - list.begin());
			value = list[step(pos, static_cast<int>(list.size()))];
			(select ? slot.editedSelect : slot.editedPose) = value;
			field = select ? "select" : "pose";
		}
		logger::info("Lobby: editor set slot{}{} = {}", index + 1, field, value);
		ComputeLayout(player->GetParentCell());
		ResettleSlot(index);
		PlaceLight(index);
		RefreshEditTargets();
	}

	void EditResetCell()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!g_editorOpen || !player) {
			return;
		}
		const auto cell = LayoutKey();
		layout::Clear(cell);
		logger::info("Lobby: editor reset '{}' to the built-in layout", cell);
		ForgetEdits();
		Summon();
	}

	void Highlight(std::size_t a_slot)
	{
		if (g_state == State::kActive) {
			PlaceLight(a_slot);
			poses::Select(a_slot);
		}
	}

	bool ShouldDismissMessageBoxes()
	{
		return rules::ShouldDismissMessageBoxes(g_state, g_revealed);
	}

	void UnlinkSeats()
	{
		std::size_t found = 0, linked = 0;
		for (const auto& slot : g_slots) {
			auto  ref = slot.view.ref.get();
			auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (actor) {
				++found;
				if (actor->extraList.GetLinkedRef(nullptr)) {
					++linked;
				}
			}
			Unlink(slot);
		}
		if (linked > 0) {
			logger::info("Lobby: unlinked {} character(s) from their furniture before the lobby's game ends", linked);
		} else if (!g_slots.empty()) {
			logger::debug("Lobby: unlink: {} of {} characters found, none linked", found, g_slots.size());
		}
	}

	void ClearPresets(std::function<void()> a_then)
	{
		const bool queued = papyrus::CallStatic("CharGen", "ClearPresets", [a_then](const RE::BSScript::Variable&) {
			logger::info("Lobby: CharGen.ClearPresets done");
			if (a_then) {
				QueueTask(a_then);
			}
		});
		if (!queued && a_then) {
			a_then();
		}
	}

	void Teardown()
	{
		BlockSaving(false);
		if (std::exchange(g_weatherForced, false)) {
			if (auto* sky = RE::Sky::GetSingleton()) {
				sky->ReleaseWeatherOverride();
				logger::info("Lobby: forced weather released");
			}
		}
		if (g_hudHidden) {
			SetHudVisible(true);
		}
	}

	void OnPreLoadGame()
	{
		// Whatever triggered the load, the real game gets saving, controls and
		// the HUD back as the load starts.
		g_loadStarted = true;
		Teardown();
		// The load frees the lobby's world, in an order we don't control.
		UnlinkSeats();
		switch (rules::OnPreLoadGame(g_state)) {
		case rules::PreLoad::kStopLobby:
			// LOAD on the select screen (the Journal). Into the loading state at
			// once, as PLAY does: the NPC-clearing timer stops (a load longer
			// than its 2 s could otherwise disable NPCs of a save in the lobby's
			// own cell) and so do the poses. A cancelled load comes back through
			// ResumeLobby, like PLAY's.
			logger::info("Lobby: a save is loading from the Journal; the lobby stops");
			SetState(State::kLoadingSave);
			poses::Reset(false);  // a cancelled load resumes this lobby
			FlushHudMessages();
			SelectMenu::Hide();
			break;
		case rules::PreLoad::kAbandonEntering:
			// A console or mod load while the lobby starts: that game is the
			// player's now. Staging must not run in it.
			logger::info("Lobby: a save is loading while the lobby is entering; abandoning the lobby");
			SelectMenu::Hide();
			ForgetLobby();
			break;
		case rules::PreLoad::kNone:
			break;
		}
	}

	void OnNewGame()
	{
		if (g_state == State::kEntering) {
			// The earliest moment the lobby's own game exists.
			BlockSaving(true);
		}
	}

	void OnJournalClosed()
	{
		// Back from the Journal (LOAD) to the select screen, if a cancelled load
		// from it had hidden ours.
		if (rules::ShouldShowSelectOnJournalClose(g_state, g_revealed, g_editorOpen, SelectMenu::IsOpen()) && g_onReady) {
			logger::info("Lobby: Journal closed; select screen back");
			g_onReady();
		}
		// Unlinked when the LOAD list opened. A load chosen there may still be
		// starting as the Journal closes, so look again in a moment.
		if (g_state == State::kActive) {
			const auto generation = g_enterGeneration;
			RunAfter(std::chrono::milliseconds(750), [generation]() {
				auto* ui = RE::UI::GetSingleton();
				if (generation == g_enterGeneration && g_state == State::kActive && !(ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME))) {
					RelinkSeats();
					logger::info("Lobby: Journal closed without a load; characters linked to their furniture again");
				}
			});
		}
	}

	bool LoadFailedRecently()
	{
		return g_loadAttempt.FailedRecently(std::chrono::steady_clock::now());
	}

	void ClearLoadFailure()
	{
		g_loadAttempt.Clear();
	}

	void NoteRuntimeForm(const RE::TESForm* a_form, std::string_view a_kind)
	{
		if (a_form) {
			g_runtimeForms.push_back({ a_form->GetFormID(), a_form, a_form->GetFormType(), std::string(a_kind) });
		}
	}

	void OnMainMenuOpened()
	{
		// Always: a failed lobby is Idle but may have left saving blocked.
		Teardown();
		// Kept: the engine may only free them once the next game starts.
		ReportRuntimeForms("the main menu opened", false);
		if (!rules::ShouldResetOnMainMenu(g_state)) {
			return;
		}
		logger::info("Lobby: main menu opened while the lobby was {}; resetting", StateName(g_state));
		ForgetLobby();
	}

	void OnPostLoadGame(bool a_success)
	{
		if (a_success) {
			ReportRuntimeForms("a load", true);
		}
		switch (rules::OnPostLoadGame(g_state, a_success)) {
		case rules::PostLoad::kIgnore:
			return;
		case rules::PostLoad::kWarnEntering:
			logger::warn("Lobby: PostLoadGame (success={}) while entering the lobby", a_success);
			return;
		case rules::PostLoad::kResume:
			// PLAY or LOAD that didn't load (the missing-content prompt, or a
			// failed load): still in the lobby until the game retries.
			logger::info("Lobby: load reported failure (missing-content prompt?); back to lobby until the retry");
			ResumeLobby();
			return;
		case rules::PostLoad::kStay:
			logger::warn("Lobby: a failed load was reported while the lobby is up; staying in the lobby");
			return;
		case rules::PostLoad::kLeave:
			break;
		}
		logger::info("Lobby: left the lobby (load success={})", a_success);
		if (a_success) {
			g_loadAttempt.Clear();
		}
		// The select menu must never outlive the lobby (after PLAY it stays up,
		// black, until the loading screen; normally hidden by then).
		if (SelectMenu::IsOpen()) {
			logger::warn("Lobby: select menu still open after the load; closing it");
			SelectMenu::Hide();
		}
		if (auto* player = RE::PlayerCharacter::GetSingleton()) {
			// Our own block was lifted at PreLoadGame (Teardown). If the loaded
			// game has saving disabled, that's its own state (a quest or a mod
			// set it): report it, don't change it.
			const bool blocked = player->GetGameStatsData().byCharGenFlag.any(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);
			if (blocked) {
				logger::warn("Lobby: saving is disabled in the loaded game (not by CharSelect, which re-enabled it before the load)");
			} else {
				logger::info("Lobby: saving is enabled in the loaded game");
			}
		}
		ForgetLobby();
	}
}
