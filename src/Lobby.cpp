// The lobby itself: starting the throwaway game, placing the characters, the
// layout editor, and loading the save the player picks (see Lobby.h).

#include "Lobby.h"

#include "Appearance.h"
#include "Config.h"
#include "Papyrus.h"
#include "Layout.h"
#include "Paths.h"
#include "Poses.h"
#include "ResetGuard.h"
#include "SelectMenu.h"
#include "Tint.h"
#include "Util.h"
#include "core/Encoding.h"
#include "core/FormKey.h"
#include "core/LayoutMath.h"
#include "core/UiRules.h"

namespace charselect::lobby
{
	namespace
	{
		// CharGen ApplyTypes: face | overrides | body morphs | transforms | skin overrides
		constexpr std::uint32_t kPresetApplyAll = 0x0F;
		constexpr RE::FormID    kPlayerBaseId = 0x7;
		// Skyrim.esm BardAudienceExcludedFaction: BardAudienceQuest's bystander
		// aliases skip its members. Without it, a bard's song in the lobby cell
		// pulls our characters off their spots to stand and applaud.
		constexpr RE::FormID    kBardAudienceExcludedFaction = 0x10FCB4;

		// After the loading screen closes, wait for the player to be settled in
		// the lobby cell before staging (the first close comes before coc has
		// finished placing the player). TrySpawn polls for that itself, so this
		// first wait is short.
		constexpr auto kSettleDelay = std::chrono::milliseconds(500);
		constexpr auto kReadyPollInterval = std::chrono::milliseconds(250);
		constexpr int  kReadyPollMax = 40;                            // 10 s
		constexpr auto kEnterWatchdog = std::chrono::seconds(30);     // coc never got past its loading screen
		constexpr auto kNoCellGrace = std::chrono::seconds(2);        // coc returned without a loading screen: no such cell
		constexpr auto kClearPresetsTimeout = std::chrono::seconds(10);
		constexpr float kMovedAway = 1000.0f;  // the player this far from the camera has been moved
		constexpr auto kRevealTimeout = std::chrono::seconds(10);     // show the menu even if a slot is slow
		constexpr auto kRevealSettle = std::chrono::milliseconds(500);  // poses in place -> let idles blend in, then show
		constexpr auto kFreezeDelay = std::chrono::milliseconds(600);  // let actors settle on the floor first

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
		std::atomic<bool> g_loadingScreenSeen{ false };  // since the last coc
		bool              g_loadingWhileActive{ false };  // a loading screen opened with the select screen up
		std::atomic<bool> g_revealed{ false };
		bool              g_revealScheduled{ false };
		bool              g_hudHidden{ false };  // UI thread only (SetHudVisible)
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
		std::atomic<std::uint32_t> g_enterGeneration{ 0 };  // invalidates stale delayed callbacks; read by CocCurrent outside the lock

		std::function<void(std::string_view, bool)> g_onFailure;
		std::function<void()>                 g_onReady;

		void TrySpawn(std::uint32_t a_generation, int a_attempt);

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
		// while the lobby is up: Ctrl sneaked and right-click drew a weapon.
		// The game menu and the console stay on. Same lifecycle as
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
			++g_enterGeneration;  // nothing the failed lobby scheduled runs on
			if (g_onFailure) {
				// The technical reason is in the log; players get a plain message.
				g_onFailure("Could not load the character select screen. See CharSelect.log for details.", false);
			}
		}

		// Runs a step of starting the lobby; an error in it fails the lobby,
		// instead of leaving the loading splash up for good.
		template <class F>
		void OrFail(std::string_view a_step, F&& a_fn)
		{
			try {
				std::forward<F>(a_fn)();
			} catch (const std::exception& e) {
				Fail(std::format("{}: unexpected error: {}", a_step, e.what()));
			} catch (...) {
				Fail(std::format("{}: unexpected error", a_step));
			}
		}

		// The game's HUD, on the UI thread (Scaleform isn't thread-safe), where
		// g_hudHidden is only used. Shown again only if hidden here.
		void SetHudVisible(bool a_visible)
		{
			QueueUITask([a_visible]() {
				if (a_visible && !g_hudHidden) {
					return;
				}
				auto* ui = RE::UI::GetSingleton();
				auto  hud = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
				if (hud && hud->uiMovie) {
					hud->uiMovie->SetVisible(a_visible);
					g_hudHidden = !a_visible;
					logger::debug("Lobby: HUD {}", a_visible ? "shown" : "hidden");
				}
			});
		}

		// Other mods' HUD widgets, hidden with the game's HUD. Widgets drawn as
		// their own always-open menus (STB Widgets and the like) are switched
		// off directly. Overlays drawn outside the game's menus (SKSE Menu
		// Framework widgets such as Minimal Vitals) can't be, but some hide
		// along with the compass, which switching the HUD movie off doesn't
		// change, so the compass is hidden too. All of it runs on the UI thread
		// (Scaleform isn't thread-safe), and this state is only used there.
		namespace overlays
		{
			struct HiddenMenu
			{
				std::string         name;
				const RE::IMenu*    menu{ nullptr };  // compared only: a reopened menu is the mod's to show
				bool                reshownLogged{ false };
			};
			std::vector<HiddenMenu>  g_menus;
			std::vector<std::string> g_clips;  // HUD clips hidden (paths)
			bool                     g_active{ false };
			std::uint32_t            g_epoch{ 0 };  // a later Hide or Restore makes a waiting Restore stale

			// Where the compass sits in the vanilla HUD and in the usual HUD
			// replacers; widgets that follow the compass look here.
			constexpr std::array kCompassPaths{ "_root.HUDMovieBaseInstance.CompassShoutMeterHolder", "_root.HUDMovieBaseInstance.Compass",
				"_root.HUDMovieBaseInstance.HUDComponent_Compass", "_root.HUDMovieBaseInstance.SkyUI_HUDComponent_Compass",
				"_root.HUDMovieBaseInstance.compass_mc", "_root.CompassShoutMeterHolder", "_root.Compass" };

			// Sets a HUD clip's visibility; true if it was found.
			bool SetClipVisible(RE::GFxMovieView* a_movie, const char* a_path, bool a_visible, bool* a_wasVisible = nullptr)
			{
				RE::GFxValue clip;
				RE::GFxValue::DisplayInfo info;
				if (!a_movie->GetVariable(&clip, a_path) || !clip.IsDisplayObject() || !clip.GetDisplayInfo(&info)) {
					return false;
				}
				if (a_wasVisible) {
					*a_wasVisible = info.GetVisible();
				}
				if (info.GetVisible() != a_visible) {
					info.SetVisible(a_visible);
					clip.SetDisplayInfo(info);
				}
				return true;
			}

			RE::GFxMovieView* HudMovie(RE::UI* a_ui)
			{
				auto hud = a_ui->GetMenu(RE::HUDMenu::MENU_NAME);
				return hud ? hud->uiMovie.get() : nullptr;
			}

			// Hides whatever of the above is showing. a_recheck: a periodic pass,
			// which only logs widgets that came back.
			void HideNow(bool a_recheck)
			{
				auto* ui = RE::UI::GetSingleton();
				if (!ui || !g_active) {
					return;
				}
				std::vector<std::string> newlyHidden;
				for (auto& [name, entry] : ui->menuMap) {
					const auto& menu = entry.menu;
					if (!menu || !menu->uiMovie || !menu->uiMovie->GetVisible() ||
						!charselect::ui::IsOtherModsHudMenu(name.c_str(), menu->menuFlags.all(RE::UI_MENU_FLAGS::kAlwaysOpen))) {
						continue;
					}
					menu->uiMovie->SetVisible(false);
					auto known = std::ranges::find_if(g_menus, [&](const HiddenMenu& h) { return h.name == name.c_str(); });
					if (known == g_menus.end()) {
						g_menus.push_back({ name.c_str(), menu.get() });
						newlyHidden.emplace_back(name.c_str());
					} else {
						known->menu = menu.get();
						if (std::exchange(known->reshownLogged, true) == false) {
							logger::debug("Lobby: other mod's HUD menu '{}' showed itself again; hidden again", known->name);
						}
					}
				}
				if (!newlyHidden.empty()) {
					std::string names;
					for (const auto& n : newlyHidden) {
						names += names.empty() ? n : ", " + n;
					}
					logger::info("Lobby: hiding other mods' HUD menus{}: {}", a_recheck ? " that appeared" : "", names);
				}

				auto* movie = HudMovie(ui);
				if (!movie) {
					return;
				}
				for (const char* path : kCompassPaths) {
					bool wasVisible = false;
					if (SetClipVisible(movie, path, false, &wasVisible) && wasVisible &&
						std::ranges::find(g_clips, path) == g_clips.end()) {
						g_clips.emplace_back(path);
						logger::debug("Lobby: HUD clip {} hidden", path);
					}
				}
			}

			void Restore(std::uint32_t a_epoch, int a_attempt)
			{
				if (a_epoch != g_epoch) {
					return;
				}
				auto* ui = RE::UI::GetSingleton();
				if (!ui) {
					return;
				}
				// During a loading screen widgets would show over it; mods that hide
				// for loads show themselves again after it, so wait for it to close.
				// The limit is only a backstop: loads on big setups can take minutes.
				constexpr auto kPoll = std::chrono::milliseconds(250);
				constexpr int  kMaxPolls = 10 * 60 * 4;  // ten minutes
				if (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) && a_attempt < kMaxPolls) {
					RunAfter(kPoll, [a_epoch, a_attempt]() {
						QueueUITask([a_epoch, a_attempt]() { Restore(a_epoch, a_attempt + 1); });
					});
					return;
				}
				int shown = 0;
				for (const auto& hidden : g_menus) {
					auto menu = ui->GetMenu(hidden.name);
					if (menu && menu.get() == hidden.menu && menu->uiMovie && !menu->uiMovie->GetVisible()) {
						menu->uiMovie->SetVisible(true);
						++shown;
					}
				}
				if (!g_menus.empty()) {
					logger::debug("Lobby: {} of {} hidden HUD menus from other mods shown again", shown, g_menus.size());
				}
				if (auto* movie = HudMovie(ui)) {
					for (const auto& path : g_clips) {
						SetClipVisible(movie, path.c_str(), true);
					}
				}
				g_menus.clear();
				g_clips.clear();
			}
		}

		void HideOverlays()
		{
			QueueUITask([]() {
				++overlays::g_epoch;
				overlays::g_active = true;
				overlays::HideNow(false);
			});
		}

		// Some widgets show themselves again a moment later (after setting up
		// once the loading screen closes): caught on the next pass.
		void KeepOverlaysHidden()
		{
			QueueUITask([]() { overlays::HideNow(true); });
		}

		void RestoreOverlays()
		{
			QueueUITask([]() {
				if (!std::exchange(overlays::g_active, false)) {
					return;
				}
				overlays::Restore(++overlays::g_epoch, 0);
			});
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
			logger::debug("Lobby: player {}: pos ({:.0f}, {:.0f}, {:.0f}) heading {:.2f} cell {:08X} '{}' attached={} 3D={}",
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
					logger::debug("Lobby:   slot {}: no reference", i + 1);
					continue;
				}
				const auto  pos = ref->GetPosition();
				auto*       actor = ref->As<RE::Actor>();
				auto*       npc = actor ? actor->GetActorBase() : nullptr;
				const auto* body = actor ? actor->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kBody) : nullptr;
				logger::debug("Lobby:   slot {} {:08X}: pos ({:.0f}, {:.0f}, {:.0f}) off-target {:.0f} dist-to-camera {:.0f} disabled={} 3D={} AI={} ready={}",
					i + 1, ref->GetFormID(), pos.x, pos.y, pos.z, pos.GetDistance(slot.pos),
					player ? pos.GetDistance(player->GetPosition()) : -1.0f, ref->IsDisabled(), ref->Is3DLoaded(),
					actor && actor->IsAIEnabled(), slot.view.ready);
				logger::debug("Lobby:     race {:08X} faceNPC {:08X} headParts {} weight {:.0f} body armor '{}'",
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
			logger::info("Lobby: {} characters in {}", scan.characters.size(), paths::ForLog(*saveDir));

			auto*      dh = RE::TESDataHandler::GetSingleton();
			const auto max = config::Get().maxCharacters;
			if (scan.characters.size() > max) {
				logger::info("Lobby: showing the {} most recently played (iMaxCharacters)", max);
			}
			for (const auto& character : scan.characters) {
				const auto  id = FormatCharacterId(character.characterId);
				const auto& latest = character.Latest();
				if (slots.size() >= max) {
					logger::debug("Lobby:   {} '{}' not shown", id, latest.header.playerName);
					continue;
				}

				// Characters without a usable appearance still get a slot (no
				// actor, still playable) instead of silently disappearing.
				auto        data = sidecar::Read(character.characterId);
				RE::TESRace* race = nullptr;
				std::string  missing;
				std::error_code ec;
				if (!data) {
					missing = "not loaded or saved with CharSelect installed yet";
				} else if (!std::filesystem::exists(paths::PresetPath(character.characterId), ec)) {
					missing = std::format("no RaceMenu preset at {}", paths::ForLog(paths::PresetPath(character.characterId)));
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
				slot.view.latestSave = text::FromUtf8(text::PathUtf8(latest.path.filename()));
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
			logger::debug("Lobby: created actor base {:08X}; copied faceNPC={:08X} outfit={:08X} containerItems={}",
				npc->GetFormID(), npc->faceNPC ? npc->faceNPC->GetFormID() : 0,
				npc->defaultOutfit ? npc->defaultOutfit->GetFormID() : 0, npc->numContainerObjects);

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
			// preset and reads faceData->parts (a null one crashes the game).
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
				logger::debug("Lobby:     spell '{}' in the {} hand", item.name, item.hand);
			}
			return equipped;
		}

		void PlaceSlot(RE::Actor* a_actor, const Slot& a_slot)
		{
			// Moving an actor in furniture leaves it sitting in mid-air. Anyone
			// else goes to its spot, seated slots included: moving a disabled
			// actor doesn't stick, and a seated slot left where it was enabled
			// starts at the camera and walks all the way to its seat.
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
			logger::debug("Lobby: created highlight light form {:08X}", light->GetFormID());
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
			g_highlighted = a_index;
			logger::debug("Lobby: highlight light {:08X} on slot {} at ({:.0f}, {:.0f}, {:.0f})", ref->GetFormID(), a_index + 1, pos.x, pos.y, pos.z);
		}

		std::uint32_t g_lightTicket{ 0 };

		// While a character is nudged in the editor (key repeat, several steps
		// a second), the light follows once the moves pause, instead of a new
		// light reference being placed on every step.
		void PlaceLightSoon(std::size_t a_index)
		{
			const auto ticket = ++g_lightTicket;
			const auto generation = g_enterGeneration.load();
			RunAfter(std::chrono::milliseconds(200), [ticket, generation, a_index]() {
				if (ticket == g_lightTicket && generation == g_enterGeneration && g_state == State::kActive) {
					PlaceLight(a_index);
				}
			});
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
			if (g_triedFallback) {
				RE::DebugMessageBox(std::format("CharSelect: The location set in CharSelect.ini (sLobbyCell = {}) couldn't be loaded, so another one "
												"is shown. Check its spelling, and that the mod it comes from is installed.",
					config::Get().lobbyCell)
										.c_str());
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
					const auto generation = g_enterGeneration.load();
					RunAfter(kReadyPollInterval, [generation]() {
						if (generation == g_enterGeneration) {
							MaybeReveal();
						}
					});
					return;
				}
			}
			g_revealScheduled = true;
			const auto generation = g_enterGeneration.load();
			RunAfter(kRevealSettle, [generation]() {
				if (generation == g_enterGeneration) {
					Reveal("all characters in place");
				}
			});
		}

		void FinishSlotFailed(std::size_t a_index, std::string_view a_why)
		{
			if (a_index < g_slots.size()) {
				{
					std::scoped_lock lock(g_lock);
					g_slots[a_index].view.failed = true;
				}
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
			if (!slot.placeholder) {
				appearance::Log3D(std::format("slot {} '{}' ready", a_index + 1, slot.data.name), actor);
			}
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
			logger::debug("Lobby: slot {} 3D loaded; equipped {}/{} worn items", a_index + 1, equipped, slot.data.worn.size());
			RunAfter(kFreezeDelay, [a_generation, a_index]() { FreezeSlot(a_generation, a_index); });
		}

		// RaceMenu can leave out saved head parts it should apply: a hair from
		// a light (ESL-flagged) plugin has been seen to become a race default,
		// though the part is found by plugin name and passes RaceMenu's own
		// sex and race rules. Any such part goes on
		// here, through the engine call RaceMenu itself uses, while the actor
		// is still disabled and before its head is built.
		void AddMissingParts(std::size_t a_index, const Slot& a_slot)
		{
			auto* npc = a_slot.npc;
			if (!npc) {
				return;
			}
			for (auto* part : appearance::SavedHeadParts(a_slot.data.characterId)) {
				const std::span parts(npc->headParts, npc->headParts ? static_cast<std::size_t>((std::max)(npc->numHeadParts, std::int8_t{ 0 })) : 0);
				if (std::ranges::find(parts, part) != parts.end()) {
					continue;
				}
				if (const auto why = appearance::WhySkipped(part, npc); !why.empty()) {
					logger::info("Lobby: slot {} saved part {:08X} {} left off, as RaceMenu does: {}", a_index + 1, part->GetFormID(),
						part->formEditorID.c_str(), why);
					continue;
				}
				npc->ChangeHeadPart(part);
				logger::info("Lobby: slot {} saved part {:08X} {} was left out by RaceMenu; put on", a_index + 1, part->GetFormID(),
					part->formEditorID.c_str());
			}
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
			if (!slot.placeholder) {
				appearance::LogRecord(std::format("slot {} '{}' after LoadCharacterEx", a_index + 1, slot.data.name), slot.npc);
				AddMissingParts(a_index, slot);
				appearance::CheckPreset(std::format("slot {} '{}' with missing parts added", a_index + 1, slot.data.name),
					slot.data.characterId, slot.npc);
			}
			actor->Enable(false);
			// AI on from the first frame, so its idle is running long before
			// the menu shows (otherwise a second of reference pose shows after
			// the splash). Its base's own top package (poses::PrepareBase) keeps it
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
		// character straight into it (with the furniture not loaded yet, the
		// character walks over to it, and can get stuck on the way).
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
				logger::debug("Lobby: slot {} furniture {:08X} 3D {} after {} ms", a_index + 1, furniture->GetFormID(),
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
			logger::debug("Lobby: slot {} '{}' placed as {:08X} (base {:08X}) at ({:.0f}, {:.0f}, {:.0f}){}", a_index + 1, slot.data.name,
				actor->GetFormID(), slot.npc->GetFormID(), slot.pos.x, slot.pos.y, slot.pos.z, slot.placeholder ? " as a hooded stand-in" : "");

			const auto generation = g_enterGeneration.load();
			if (slot.placeholder) {
				// No preset to apply: the copied base already has a whole head.
				// Enabled a moment later, not now: enabling in the same frame as
				// the Disable() above leaves it disabled for good.
				RunAfter(kReadyPollInterval, [generation, a_index]() { OnPresetApplied(generation, a_index, true); });
				return;
			}

			// LoadCharacterEx points the face tint at the exported DDS; make sure
			// something is there so a failed export can't render pink.
			tint::EnsurePlaceholder(slot.data.characterId);

			const bool queued = papyrus::CallStatic("CharGen", "LoadCharacterEx",
				[generation, a_index](const RE::BSScript::Variable& a_result) {
					const bool ok = a_result.IsBool() && a_result.GetBool();
					logger::debug("Lobby: CharGen.LoadCharacterEx for slot {} returned {}", a_index + 1, ok);
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

		// Someone to hide: an enabled actor that's neither the player nor ours.
		bool ShouldHide(RE::Actor* a_actor)
		{
			return a_actor && !a_actor->IsPlayerRef() && !a_actor->IsDisabled() && !IsSlotActor(a_actor);
		}

		// In the lobby as ForEachLobbyRef sees it: the player's cell indoors,
		// within kOutdoorRadius outdoors.
		bool InLobby(RE::TESObjectREFR* a_ref)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* cell = player ? player->GetParentCell() : nullptr;
			if (!cell || !a_ref) {
				return false;
			}
			if (cell->IsInteriorCell()) {
				return a_ref->GetParentCell() == cell;
			}
			return a_ref->GetPosition().GetDistance(player->GetPosition()) <= kOutdoorRadius;
		}

		// Hides the lobby's own NPCs. They keep their schedules in the lobby's
		// game, so anyone walking in later has to be hidden too: at once when
		// their 3D loads (OnObjectLoaded), and by the slow sweep for anyone
		// that misses.
		std::size_t HideOtherActors()
		{
			std::size_t hidden = 0;
			std::string names;
			ForEachLobbyRef([&](RE::TESObjectREFR* a_ref) {
				auto* actor = a_ref ? a_ref->As<RE::Actor>() : nullptr;
				if (ShouldHide(actor)) {
					// With its 3D already in, OnObjectLoaded missed it (could have been
					// seen); without, the sweep got there before the 3D did.
					const bool  loaded = actor->Is3DLoaded();
					actor->Disable();
					const char* name = actor->GetDisplayFullName();
					names += std::format("{}{:08X} '{}' ({})", hidden ? ", " : "", actor->GetFormID(), name ? name : "",
						loaded ? "3D loaded" : "before its 3D");
					++hidden;
				}
				return RE::BSContainer::ForEachResult::kContinue;
			});
			if (hidden > 0) {
				logger::info("Lobby: hid {} other actor(s)", hidden);
				logger::debug("Lobby:   {}", names);
			}
			return hidden;
		}

		// Two sweeps while the lobby is up. The fast one is for cheap checks
		// where being late shows on screen (the camera); the slow one for the
		// heavier ones (scanning the cell, other mods' HUD menus).
		constexpr auto kFastSweepInterval = std::chrono::milliseconds(100);
		constexpr auto kSlowSweepInterval = std::chrono::milliseconds(500);

		// The game is going back to the main menu (MAIN MENU, or the Journal's
		// Quit): the engine sets this before it tears the lobby's world down.
		bool GameResetting()
		{
			auto* main = RE::Main::GetSingleton();
			return main && main->GetRuntimeData().resetGame;
		}

		std::uint32_t           g_cameraFixes{ 0 };  // this lobby: times first person was put back
		rules::CameraFixLimiter g_cameraLimiter;

		// The lobby is seen through its player's eyes (PlacePlayer). Mods that
		// switch to third person, such as Immersive Equipping Animations when
		// something equips the lobby's player at startup, would show that player
		// from behind instead, so first person is put back; but not a camera
		// at the player's head (another mod's first person), and not against a
		// mod that keeps switching it away (they'd fight every frame).
		void KeepFirstPerson()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!camera || !player || g_cameraLimiter.GaveUp() || camera->IsInFirstPerson()) {
				return;
			}
			const float toHead = camera->cameraRoot ? camera->cameraRoot->world.translate.GetDistance(player->GetLookingAtLocation())
			                                        : (std::numeric_limits<float>::max)();
			if (!rules::ShouldForceFirstPerson(false, toHead)) {
				return;
			}
			const int state = camera->currentState ? static_cast<int>(camera->currentState->id) : -1;
			if (!g_cameraLimiter.Allow(std::chrono::steady_clock::now())) {
				logger::warn("Lobby: another mod keeps taking the camera out of first person (camera state {}); leaving it to that mod", state);
				return;
			}
			camera->ForceFirstPerson();
			if (++g_cameraFixes == 1) {
				logger::info("Lobby: something took the camera out of first person (camera state {}, {:.0f} units from the head); put back", state, toHead);
			} else if (g_cameraFixes <= 5) {
				logger::debug("Lobby: camera out of first person again (camera state {}, {:.0f} units from the head); put back", state, toHead);
			}
		}

		// Each StartSweeps makes the sweeps already running stale, so a lobby
		// resumed after a cancelled load has one pair, not one more each time.
		std::uint32_t g_sweepTicket{ 0 };

		void FastSweep(std::uint32_t a_ticket)
		{
			if (a_ticket != g_sweepTicket || g_state != State::kActive) {
				return;
			}
			if (GameResetting()) {
				return;  // the slow sweep ends the lobby
			}
			KeepFirstPerson();
			RunAfter(kFastSweepInterval, [a_ticket]() { FastSweep(a_ticket); });
		}

		void SlowSweep(std::uint32_t a_ticket)
		{
			if (a_ticket != g_sweepTicket || g_state != State::kActive) {
				return;
			}
			if (GameResetting()) {
				BeginLeaving("the game is going back to the main menu");
				return;
			}
			HideOtherActors();
			KeepOverlaysHidden();
			RunAfter(kSlowSweepInterval, [a_ticket]() { SlowSweep(a_ticket); });
		}

		// Both stop by themselves once the lobby isn't active.
		void StartSweeps()
		{
			const auto ticket = ++g_sweepTicket;
			RunAfter(kFastSweepInterval, [ticket]() { FastSweep(ticket); });
			RunAfter(kSlowSweepInterval, [ticket]() { SlowSweep(ticket); });
		}

		// Layouts are stored under the location the lobby was started in
		// (sLobbyCell, or the fallback): outdoors the player can stand in a
		// neighbouring cell with another name, or none.
		const std::string& LayoutKey()
		{
			return g_targetCell;
		}

		// Drops the slot actor's link to its furniture. A link must never
		// outlive its target: when the actor is destroyed, the engine reads the
		// target to unregister the link, and a freed target crashes the game
		// (a load from the lobby can free the cell's chairs before the
		// characters seated on them). So: unlink before deleting furniture we
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
				std::abs(geometry::WrapAngle(existing->GetAngleZ() - a_slot.heading)) < 0.01f) {
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
			}
			existing->Disable();
			existing->SetPosition(a_slot.pos);
			existing->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, a_slot.heading });
			existing->Enable(false);
			a_slot.placedFurniture = existing->CreateRefHandle();
			logger::debug("Lobby: placed furniture {:08X} (base {:08X}) at ({:.0f}, {:.0f}, {:.0f}) heading {:.2f}", existing->GetFormID(),
				a_base->GetFormID(), a_slot.pos.x, a_slot.pos.y, a_slot.pos.z, a_slot.heading);
			return a_slot.placedFurniture;
		}

		using SeatSpot = geometry::Seat;

		// A spot just in front of one of a_furniture's sit markers, facing the
		// way a sitter faces, so the sit package seats its character on the
		// spot instead of walking it over.
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
				const float heading = geometry::WrapAngle(baseHeading + marker.heading);
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
				logger::debug("Lobby: using saved camera ({:.0f}, {:.0f}, {:.0f}) yaw {:.2f} pitch {:.2f}",
					g_stage.camera.x, g_stage.camera.y, g_stage.camera.z, g_stage.cameraYaw, g_stage.cameraPitch);
			} else {
				g_stage.camera = g_stage.spawn;
				g_stage.cameraYaw = g_stage.spawnYaw;
				g_stage.cameraPitch = kDefaultPitch;
				logger::debug("Lobby: no saved camera for '{}'; using where the player arrived", LayoutKey());
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
					logger::debug("Lobby: slot {} using saved spot ({:.0f}, {:.0f}, {:.0f}) heading {:.2f}", i + 1, slot.pos.x, slot.pos.y, slot.pos.z, slot.heading);
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
				logger::debug("Lobby: slot {} has no saved spot; standing in the row in front of the camera", i + 1);
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
					logger::debug("Lobby: slot {} waits at seat {} of {:08X}: ({:.0f}, {:.0f}, {:.0f}) heading {:.2f}", i + 1, seat->marker,
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

		void                       TryFallbackOrFail(std::string_view a_reason);
		std::atomic<std::uint32_t> g_cocTicket{ 0 };

		bool CocCurrent(std::uint32_t a_ticket, std::uint32_t a_generation)
		{
			return a_ticket == g_cocTicket && a_generation == g_enterGeneration && g_state == State::kEntering;
		}

		// Gives up on a coc whose loading screen never finished; one still
		// loading (a big load order, a slow drive) gets more time. Each coc
		// has its own, so a fallback cell gets the full time too.
		void WatchCoc(std::uint32_t a_ticket, std::uint32_t a_generation)
		{
			RunAfter(kEnterWatchdog, [a_ticket, a_generation]() {
				if (!CocCurrent(a_ticket, a_generation) || g_spawnScheduled) {
					return;
				}
				auto* ui = RE::UI::GetSingleton();
				if (ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
					logger::info("Lobby: still loading '{}' after {} s; waiting", g_targetCell, kEnterWatchdog.count());
					WatchCoc(a_ticket, a_generation);
					return;
				}
				// Its loading screen came and went: the coc finished. coc holds up
				// the task queue while it loads, so on a long load this check runs
				// only as it returns, ahead of OnLoadingMenuClosed's task; staging
				// takes over from there, with its own wait (TrySpawn).
				if (g_loadingScreenSeen) {
					logger::debug("Lobby: '{}' finished loading as the watchdog ran; staging takes over", g_targetCell);
					return;
				}
				LogPlayer("watchdog");
				TryFallbackOrFail(std::format("'{}' never finished loading", g_targetCell));
			});
		}

		void IssueCoc(std::string_view a_cell)
		{
			g_targetCell = std::string(a_cell);
			g_spawnScheduled = false;
			g_loadingScreenSeen = false;
			const auto ticket = ++g_cocTicket;
			const auto generation = g_enterGeneration.load();
			// Outside GameLock: coc loads the cell before it returns, and the
			// menu events and tasks of that load take the lock on other threads.
			QueueTaskUnlocked([command = std::format("coc {}", a_cell), ticket, generation]() {
				if (!CocCurrent(ticket, generation)) {
					return;  // the lobby was abandoned or failed before this ran
				}
				const bool ran = RunConsoleCommand(command);
				QueueTask([ran, ticket, generation]() {
					if (!CocCurrent(ticket, generation)) {
						return;
					}
					if (!ran) {
						Fail("console command failed");
						return;
					}
					// coc returns once its cell has loaded; with no loading screen
					// at all, the game had no cell by that name.
					RunAfter(kNoCellGrace, [ticket, generation]() {
						if (CocCurrent(ticket, generation) && !g_loadingScreenSeen) {
							TryFallbackOrFail(std::format("no cell named '{}'", g_targetCell));
						}
					});
				});
			});
			WatchCoc(ticket, generation);
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
					logger::debug("Lobby: time of day set to {:.2f}", calendar->gameHour->value);
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
					logger::debug("Lobby: weather set to {:08X}", weather->GetFormID());
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
			g_cameraFixes = 0;
			g_cameraLimiter.Reset();
			LogPlayer("at staging");
			HideOtherActors();
			ApplyTimeAndWeather();
			SetHudVisible(false);
			HideOverlays();
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
			const auto generation = g_enterGeneration.load();
			RunAfter(kRevealTimeout, [generation]() {
				if (generation == g_enterGeneration && !g_revealed && g_state == State::kActive) {
					logger::warn("Lobby: reveal timeout, showing the menu with unfinished slots");
					for (auto& s : g_slots) {
						s.finished = true;
					}
					Reveal("reveal timeout");
				}
			});
			StartSweeps();
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
			OrFail("staging the lobby", [player]() { StageLobby(player); });
		}

		// Notifications mods send while starting up in the lobby go to the
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
					logger::debug("Lobby: HUD notification queue not found; nothing cleared");
					return;
				}
				const auto queued = queue.GetArraySize();
				if (queued > 0) {
					queue.ClearElements();
				}
				logger::debug("Lobby: {} HUD notifications from the lobby were still queued{}", queued, queued > 0 ? "; cleared" : "");
			});
		}

		constexpr auto kLoadStartTimeout = std::chrono::seconds(20);
		// When the last PLAY/LOAD from the lobby started; cleared once a load
		// succeeds. A main menu soon after means the load failed (loop breaker).
		rules::LoadFailureTracker g_loadAttempt;
		bool                      g_loadStarted{ false };  // PreLoadGame seen since LoadFile
		// The load list (the Journal) took ResetGuard's capture as it opened:
		// quitting to the main menu from there leaves the lobby's game without
		// PreLoadGame or MAIN MENU. Dropped again if it closes without either.
		bool g_loadListCapture{ false };
		bool g_loadSinceLoadList{ false };

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
			g_loadListCapture = false;
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
		// LoadFile stopped (the poses, the sweeps, the hidden HUD).
		void ResumeLobby()
		{
			SetState(State::kActive);
			BlockSaving(true);
			SetHudVisible(false);
			HideOverlays();
			// Unlinked for the load (LoadFile, or the Journal's load list). Not
			// while the Journal is still up (the missing-content prompt): a load
			// confirmed there would free the world with the links in place, and
			// by its PreLoadGame they can't be undone. OnJournalClosed links them.
			auto* ui = RE::UI::GetSingleton();
			const bool journalOpen = ui && ui->IsMenuOpen(RE::JournalMenu::MENU_NAME);
			if (!journalOpen) {
				RelinkSeats();
			}
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				if (g_slots[i].view.ready) {
					ConfigurePoses(i);
					poses::Start(i);
				}
			}
			poses::Select(g_highlighted);
			PlaceLight(g_highlighted);
			StartSweeps();
			// The select screen again (out of the fade to black after PLAY); after
			// a LOAD, once the Journal closes (OnJournalClosed).
			if (!journalOpen && g_onReady) {
				g_onReady();
			}
		}

		// Each PLAY gets its own ticket, so a watchdog left from an earlier one
		// (a fast failure, then PLAY again) can't resume the lobby under it.
		std::uint32_t g_loadTicket{ 0 };

		// Checks a PLAY load has started. A message box (a mod's, or any prompt
		// waiting on the player; PLAY itself skips the missing-content prompt)
		// or the loading screen being up means it's still under way: look again.
		void WatchLoadStart(std::uint32_t a_ticket)
		{
			RunAfter(kLoadStartTimeout, [a_ticket]() {
				auto*      ui = RE::UI::GetSingleton();
				const bool waiting = ui && (ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) || ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME));
				switch (rules::CheckLoadStart(a_ticket == g_loadTicket, g_state, g_loadStarted, waiting)) {
				case rules::LoadWatch::kStop:
					return;
				case rules::LoadWatch::kRecheck:
					WatchLoadStart(a_ticket);
					return;
				case rules::LoadWatch::kResume:
					logger::warn("Lobby: the load never started; back to the select screen");
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
			// longer be found, though the world is still freed after it.
			// ResumeLobby links them again.
			UnlinkSeats();
			// Saving, controls and the HUD stay as they are until the load
			// actually starts (OnPreLoadGame): ClearPresets is a Papyrus round
			// trip that can take seconds on a busy VM, and an autosave in that
			// gap would save the lobby game.
			FlushHudMessages();

			// A load that never starts would leave the screen black (the select
			// menu stays up, black, until the loading screen): come back instead.
			WatchLoadStart(++g_loadTicket);

			auto load = [file = std::move(a_file), generation = g_enterGeneration.load(), ticket = g_loadTicket]() {
				// ClearPresets is a Papyrus round trip: by now the watchdog may
				// have resumed the lobby, or another PLAY may have started.
				if (generation != g_enterGeneration || ticket != g_loadTicket || g_state != State::kLoadingSave) {
					logger::info("Lobby: not loading {}: the lobby moved on while RaceMenu cleared its presets", file);
					return;
				}
				auto* manager = RE::BGSSaveLoadManager::GetSingleton();
				if (!manager) {
					logger::error("Lobby: BGSSaveLoadManager unavailable");
					return;
				}
				// CommonLib calls this flag a_checkForMods, and its one-argument
				// Load passes true, but what it does (seen in game): true
				// loads without the missing-content prompt (a documented
				// limitation); false abandons the load and returns to the main
				// menu, which looped with the lobby re-entering. So it reads as
				// "mods already checked, go ahead": the game's own load menu
				// passes true after the player accepts its prompt.
				logger::debug("Lobby: BGSSaveLoadManager::Load('{}', true)", file);
				// Outside GameLock: PreLoadGame follows on the main thread and
				// takes it (see QueueTaskUnlocked).
				QueueTaskUnlocked([manager, file]() { manager->Load(file.c_str(), true); });
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

	std::vector<SlotStatus> GetSlotStatus()
	{
		std::scoped_lock        lock(g_lock);
		std::vector<SlotStatus> out;
		out.reserve(g_slots.size());
		for (const auto& s : g_slots) {
			out.push_back({ s.view.ready, s.view.failed, s.view.ref });
		}
		return out;
	}

	void SetFailureHandler(std::function<void(std::string_view, bool)> a_handler)
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

		std::vector<Slot> slots;
		bool              read = false;
		OrFail("reading the saves", [&]() {
			slots = CollectSlots();
			read = true;
		});
		if (!read) {
			return;
		}
		if (slots.empty()) {
			if (g_onFailure) {
				g_onFailure("No saved characters found.", true);
			}
			return;
		}
		{
			std::scoped_lock lock(g_lock);
			g_slots = std::move(slots);
		}
		++g_enterGeneration;
		g_triedFallback = false;
		g_loadingWhileActive = false;
		g_revealed = false;
		g_revealScheduled = false;
		g_editorOpen = false;
		// Forms from a previous lobby game may be gone (e.g. after Back to main menu).
		g_light.reset();
		g_lightBase = nullptr;
		g_highlighted = 0;
		g_stage = {};
		g_cameraEdited = false;
		poses::Reset(true);

		SetState(State::kEntering);
		IssueCoc(config::Get().lobbyCell);
	}

	void OnLoadingMenuClosed()
	{
		if (g_state == State::kActive) {
			if (!std::exchange(g_loadingWhileActive, false)) {
				return;  // the coc's own loading screen, closing late
			}
			// A loading screen while the select screen is up: a mod's script
			// moved the player (an intro or alternate start running late, for
			// example). If they're no longer at the camera, the lobby can't go
			// on, and the main menu is the way back.
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && player->GetPosition().GetDistance(g_stage.camera) > kMovedAway) {
				LogPlayer("after a loading screen in the lobby");
				Fail("the player was moved out of the lobby by a loading screen (another mod's script?)");
			}
			return;
		}
		if (g_state != State::kEntering) {
			return;
		}
		LogPlayer("at loading screen close");
		// The lobby game is running: no saving from here on.
		BlockSaving(true);
		// The engine closes our menu when the loading screen opens; cover the
		// scene again until everyone is in place (otherwise actors are seen
		// popping in and getting dressed).
		if (!SelectMenu::IsOpen()) {
			SelectMenu::ShowSplash("Loading characters");
		}
		if (g_spawnScheduled) {
			return;
		}
		g_spawnScheduled = true;
		const auto generation = g_enterGeneration.load();
		RunAfter(kSettleDelay, [generation]() { TrySpawn(generation, 0); });
	}

	void Play(std::size_t a_slot)
	{
		if (!rules::CanPlay(g_state, a_slot, g_slots.size(), a_slot < g_slots.size() && !g_slots[a_slot].view.latestSave.empty())) {
			logger::info("Lobby: Play on invalid slot {} (state {})", a_slot + 1, StateName(g_state));
			if (g_state == State::kActive) {
				SelectMenu::ShowSelect();  // back from the fade to black
			}
			return;
		}
		LoadFile(g_slots[a_slot].view.latestSave, g_slots[a_slot].data.name);
	}

	namespace
	{
		std::size_t             g_editReturnHighlight{ 0 };
		std::vector<EditTarget> g_editTargets;  // snapshot for the menu (UI thread), under g_lock

		constexpr auto kReseatDelay = std::chrono::milliseconds(700);

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
				g_stage.camera.z, "   ", geometry::Degrees(g_stage.cameraYaw), "   ", static_cast<int>(std::lround(g_stage.cameraPitch * 180.0f / RE::NI_PI)));
			targets.push_back(std::move(camera));
			for (std::size_t i = 0; i < g_slots.size(); ++i) {
				const auto& slot = g_slots[i];
				EditTarget  t;
				t.label = std::format("{}  {}", i + 1, slot.view.name);
				t.kind = KindOf(slot);
				t.detail = std::format("x {:.0f}  y {:.0f}  z {:.0f}   facing {}\xC2\xB0", slot.pos.x, slot.pos.y, slot.pos.z, geometry::Degrees(slot.heading));
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
					const auto generation = g_enterGeneration.load();
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
			PlaceLightSoon(a_index);
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
				logger::debug("Lobby: slot {} leaves its seat for ({:.0f}, {:.0f}, {:.0f})", a_index + 1, slot.pos.x, slot.pos.y, slot.pos.z);
				actor->StopInteractingQuick(false);
				actor->SetPosition(slot.pos, true);
				actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, slot.heading });
				const auto token = ++slot.reseat;
				const auto generation = g_enterGeneration.load();
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
		if (g_state != State::kActive || !g_revealed) {
			return;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		logger::debug("Lobby: recomputing layout");
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

	bool IsRevealed()
	{
		return g_revealed;
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
		if (g_state != State::kActive || !g_revealed || g_editorOpen) {
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
			g_stage.cameraYaw = geometry::WrapAngle(g_stage.cameraYaw + a_turn);
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
			slot.heading = geometry::WrapAngle(slot.heading + a_turn);
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

	std::size_t HighlightedSlot()
	{
		return g_highlighted;
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
		// a_then runs once, as a task: when RaceMenu answers, or after
		// kClearPresetsTimeout if a Papyrus VM busy with other mods' scripts
		// hasn't, rather than leave the screen black.
		auto then = std::make_shared<std::function<void()>>(std::move(a_then));
		const auto run = [then](bool a_answered) {
			if (auto fn = std::exchange(*then, {})) {
				if (!a_answered) {
					logger::warn("Lobby: CharGen.ClearPresets hasn't finished after {} s; going on", kClearPresetsTimeout.count());
				}
				fn();
			}
		};
		const bool queued = papyrus::CallStatic("CharGen", "ClearPresets", [run](const RE::BSScript::Variable&) {
			logger::debug("Lobby: CharGen.ClearPresets done");
			QueueTask([run]() { run(true); });
		});
		if (!queued) {
			run(true);
			return;
		}
		RunAfter(kClearPresetsTimeout, [run]() { run(false); });
	}

	void BeginLeaving(std::string_view a_why)
	{
		if (g_state == State::kLeaving) {
			return;
		}
		logger::info("Lobby: leaving ({})", a_why);
		poses::Reset(false);  // nobody sits back down (and links again) while the world goes
		UnlinkSeats();
		g_editorOpen = false;
		++g_enterGeneration;  // every delayed callback goes stale
		SetState(State::kLeaving);
	}

	void OnLoadListOpened()
	{
		// Opened again before the last close was handled: that capture is still
		// the load list's to drop.
		g_loadListCapture = resetguard::Capture("load list") || g_loadListCapture;
		g_loadSinceLoadList = false;
	}

	void OnLoadingMenuOpened()
	{
		if (g_state == State::kEntering) {
			g_loadingScreenSeen = true;
		} else if (g_state == State::kActive) {
			g_loadingWhileActive = true;
		}
	}

	void Teardown()
	{
		BlockSaving(false);
		if (std::exchange(g_weatherForced, false)) {
			if (auto* sky = RE::Sky::GetSingleton()) {
				sky->ReleaseWeatherOverride();
				logger::debug("Lobby: forced weather released");
			}
		}
		SetHudVisible(true);
		RestoreOverlays();
	}

	void OnPreLoadGame()
	{
		// Whatever triggered the load, the real game gets saving, controls and
		// the HUD back as the load starts.
		g_loadStarted = true;
		g_loadSinceLoadList = true;
		Teardown();
		// The load frees the lobby's world, in an order we don't control.
		UnlinkSeats();
		switch (rules::OnPreLoadGame(g_state)) {
		case rules::PreLoad::kStopLobby:
			// LOAD on the select screen (the Journal). Into the loading state at
			// once, as PLAY does: the sweeps stop (the slow one could
			// otherwise disable NPCs of a save in the lobby's own cell) and so
			// do the poses. A cancelled load comes back through ResumeLobby,
			// like PLAY's.
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

	void OnObjectLoaded(RE::FormID a_formID)
	{
		if (g_state != State::kActive) {
			return;
		}
		auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_formID);
		if (!ShouldHide(actor) || !InLobby(actor)) {
			return;
		}
		actor->Disable();
		const char* name = actor->GetDisplayFullName();
		logger::debug("Lobby: hid {:08X} '{}' as its 3D loaded", actor->GetFormID(), name ? name : "");
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
		if (g_state == State::kActive && GameResetting()) {
			BeginLeaving("Quit to the main menu from the Journal");
			return;
		}
		// Back from the Journal (LOAD) to the select screen, if a cancelled load
		// from it had hidden ours.
		if (rules::ShouldShowSelectOnJournalClose(g_state, g_revealed, g_editorOpen, SelectMenu::IsOpen()) && g_onReady) {
			logger::info("Lobby: Journal closed; select screen back");
			g_onReady();
		}
		// Unlinked when the LOAD list opened. A load chosen there may still be
		// starting as the Journal closes, or a quit to the main menu, so look
		// again in a moment.
		if (g_state == State::kActive) {
			const auto generation = g_enterGeneration.load();
			RunAfter(std::chrono::milliseconds(750), [generation]() {
				// Not if the load list was opened again meanwhile (still unlinked
				// for it, and its own close looks again).
				auto* ui = RE::UI::GetSingleton();
				if (generation != g_enterGeneration || g_state != State::kActive ||
					(ui && (ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) || ui->IsMenuOpen(RE::JournalMenu::MENU_NAME)))) {
					return;
				}
				if (GameResetting()) {
					BeginLeaving("Quit to the main menu from the Journal");
					return;
				}
				RelinkSeats();
				logger::info("Lobby: Journal closed without a load; characters linked to their furniture again");
				if (std::exchange(g_loadListCapture, false) && !g_loadSinceLoadList) {
					resetguard::Discard("the load list closed");
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

	void OnMainMenuOpened()
	{
		// Always: a failed lobby is Idle but may have left saving blocked.
		Teardown();
		if (!rules::ShouldResetOnMainMenu(g_state)) {
			return;
		}
		logger::info("Lobby: main menu opened while the lobby was {}; resetting", StateName(g_state));
		ForgetLobby();
	}

	void OnPostLoadGame(bool a_success)
	{
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
