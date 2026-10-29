// Records each character's appearance whenever they're saved or loaded, so
// the lobby can show them later (see Sidecar.h).

#include "Sidecar.h"

#include "Appearance.h"
#include "Lobby.h"
#include "Papyrus.h"
#include "Paths.h"
#include "SaveIndex.h"
#include "Tint.h"
#include "Util.h"
#include "core/Encoding.h"
#include "core/ExportTracker.h"
#include "core/Files.h"

#include <nlohmann/json.hpp>

namespace charselect::sidecar
{
	namespace
	{
		using json = nlohmann::json;

		std::mutex                                                                  g_lock;
		std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> g_lastExport;
		bool                                                                        g_raceMenuDirty{ false };

		// The tint export reads the player's rendered face, which can fail
		// (first person is the prime suspect); failed exports are retried once
		// the player is in third person. Every export is tied to the player at
		// the time it was asked for: once the player is someone else, it's
		// dropped rather than saving their face under the old ID.
		ExportTracker g_exports;
		bool          g_tintWatcher{ false };

		void ExportPreset(ExportTracker::Ticket a_ticket);

		// The ticket's character is still the player, and the lobby isn't up.
		bool StillCurrent(const ExportTracker::Ticket& a_ticket)
		{
			std::scoped_lock lock(g_lock);
			return g_exports.IsCurrent(a_ticket) && lobby::GetState() == lobby::State::kIdle;
		}

		bool InThirdPerson()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			return camera && camera->IsInThirdPerson();
		}

		void TintWatcherTick()
		{
			std::vector<ExportTracker::Ticket> ready;
			{
				std::scoped_lock lock(g_lock);
				if (!g_exports.HasPending()) {
					g_tintWatcher = false;
					return;
				}
				auto* ui = RE::UI::GetSingleton();
				if (InThirdPerson() && ui && !ui->GameIsPaused() && lobby::GetState() == lobby::State::kIdle) {
					ready = g_exports.Pending();
				}
			}
			for (const auto& ticket : ready) {
				logger::info("Sidecar: retrying tint export for {} now that the player is in third person", FormatCharacterId(ticket.characterId));
				ExportPreset(ticket);
			}
			RunAfter(std::chrono::seconds(5), TintWatcherTick);
		}

		void CheckTint(ExportTracker::Ticket a_ticket, bool a_thirdPerson)
		{
			const auto id = a_ticket.characterId;
			if (tint::HasRealExport(id)) {
				logger::info("Sidecar: tint texture for {} exported OK", FormatCharacterId(id));
				std::scoped_lock lock(g_lock);
				g_exports.Succeeded(a_ticket);
				return;
			}
			std::scoped_lock lock(g_lock);
			switch (g_exports.Failed(a_ticket)) {
			case ExportTracker::Outcome::kStale:
				logger::info("Sidecar: no tint texture for {}, but the player has changed since; not retrying", FormatCharacterId(id));
				return;
			case ExportTracker::Outcome::kGiveUp:
				logger::warn("Sidecar: giving up on the tint export for {}; the lobby will use a flat skin tone", FormatCharacterId(id));
				return;
			case ExportTracker::Outcome::kRetry:
				logger::info("Sidecar: no tint texture was exported for {} (attempt {}, camera was {} person)", FormatCharacterId(id),
					g_exports.Failures(id), a_thirdPerson ? "third" : "first");
				if (!g_tintWatcher) {
					g_tintWatcher = true;
					RunAfter(std::chrono::seconds(5), TintWatcherTick);
				}
				return;
			}
		}

		std::optional<FormRef> MakeFormRef(const RE::TESForm* a_form)
		{
			// Not GetFile(0): an item WACCF edits was recorded under WACCF (0.5.4).
			const auto owner = OwningPlugin(a_form);
			if (!owner) {
				return std::nullopt;
			}
			const char* name = a_form->GetName();
			return FormRef{ text::ToUtf8(owner->file->GetFilename()), owner->localId, text::ToUtf8(name ? name : "") };
		}

		// A worn item generated at runtime (FF form ID, no plugin to look it up
		// in later): logged so we learn what mods make these, and replaced by
		// the item it was made from, which looks the same, if it has one.
		std::optional<FormRef> GeneratedItemRef(RE::TESBoundObject* a_obj)
		{
			RE::TESBoundObject* made = nullptr;
			std::string         detail;
			if (auto* weapon = a_obj->As<RE::TESObjectWEAP>()) {
				made = weapon->templateWeapon;
				detail = std::format("weapon type {}", static_cast<int>(weapon->GetWeaponType()));
			} else if (auto* armor = a_obj->As<RE::TESObjectARMO>()) {
				made = armor->templateArmor;
				detail = std::format("armor slots {:08X}", armor->GetSlotMask().underlying());
			}
			const char* name = a_obj->GetName();
			auto        ref = made ? MakeFormRef(made) : std::nullopt;
			logger::debug("Sidecar:   worn item {:08X} '{}' was generated at runtime (form type {}, {}); made from {}", a_obj->GetFormID(), name ? name : "",
				static_cast<int>(a_obj->GetFormType()), detail.empty() ? "-" : detail,
				ref ? std::format("{} {:08X} '{}', saved instead", ref->plugin, ref->localId, ref->name) :
					  made ? std::format("{:08X}, also generated; skipped", made->GetFormID()) : "nothing; skipped");
			return ref;
		}

		bool IsTwoHanded(const RE::TESObjectWEAP* a_weapon)
		{
			return a_weapon->IsTwoHandedSword() || a_weapon->IsTwoHandedAxe() || a_weapon->IsBow() || a_weapon->IsCrossbow();
		}

		std::vector<FormRef> WornItems(RE::PlayerCharacter* a_player)
		{
			std::vector<FormRef> out;
			auto inventory = a_player->GetInventory([](RE::TESBoundObject& a_obj) {
				return a_obj.IsArmor() || a_obj.IsWeapon() || a_obj.Is(RE::FormType::Light) || a_obj.IsAmmo();
			});
			const auto* left = a_player->GetEquippedObject(true);
			const auto* right = a_player->GetEquippedObject(false);
			for (const auto& [obj, data] : inventory) {
				const auto& entry = data.second;
				if (!entry || !entry->IsWorn()) {
					continue;
				}
				auto ref = obj->IsDynamicForm() ? GeneratedItemRef(obj) : MakeFormRef(obj);
				if (!ref) {
					if (!obj->IsDynamicForm()) {
						logger::debug("  worn item {:08X} '{}' skipped (no source file)", obj->GetFormID(), obj->GetName() ? obj->GetName() : "");
					}
					continue;
				}
				// Weapons go back in the hand they were in; a pair of the same
				// weapon is one inventory entry worn in both hands.
				auto* weapon = obj->As<RE::TESObjectWEAP>();
				if (weapon && !IsTwoHanded(weapon)) {
					if (left == obj) {
						auto inLeft = *ref;
						inLeft.hand = "left";
						out.push_back(std::move(inLeft));
					}
					if (right == obj) {
						ref->hand = "right";
						out.push_back(std::move(*ref));
					} else if (left != obj) {
						out.push_back(std::move(*ref));  // worn but in neither hand?
					}
					continue;
				}
				out.push_back(std::move(*ref));
			}
			return out;
		}

		std::vector<FormRef> HandSpells(RE::PlayerCharacter* a_player)
		{
			std::vector<FormRef> out;
			for (const bool leftHand : { true, false }) {
				auto* form = a_player->GetEquippedObject(leftHand);
				auto* spell = form ? form->As<RE::SpellItem>() : nullptr;
				if (!spell) {
					continue;
				}
				if (auto ref = MakeFormRef(spell)) {
					ref->hand = leftHand ? "left" : "right";
					out.push_back(std::move(*ref));
				} else {
					logger::info("Sidecar:   spell {:08X} '{}' in the {} hand was generated at runtime; skipped", spell->GetFormID(),
						spell->GetName() ? spell->GetName() : "", leftHand ? "left" : "right");
				}
			}
			return out;
		}

		bool ShouldExportPreset(std::uint32_t a_id)
		{
			std::scoped_lock lock(g_lock);
			const auto now = std::chrono::steady_clock::now();
			const auto it = g_lastExport.find(a_id);
			const bool presetMissing = !std::filesystem::exists(paths::PresetPath(a_id)) || !tint::HasRealExport(a_id);
			const auto last = it == g_lastExport.end() ? std::nullopt : std::optional(it->second);
			if (NeedsPresetExport(presetMissing, last, now, g_raceMenuDirty)) {
				logger::debug("Sidecar: preset export needed (missing={}, stale={}, raceMenuDirty={})", presetMissing,
					!last || now - *last >= kPresetExportInterval, g_raceMenuDirty);
				g_lastExport[a_id] = now;
				g_raceMenuDirty = false;
				return true;
			}
			return false;
		}

		void ExportPreset(ExportTracker::Ticket a_ticket)
		{
			const auto a_id = a_ticket.characterId;
			if (!StillCurrent(a_ticket)) {
				logger::info("Sidecar: not exporting {}: the player has changed since it was asked for", FormatCharacterId(a_id));
				return;
			}
			const auto name = paths::PresetName(a_id);
			const auto start = std::chrono::steady_clock::now();
			const bool thirdPerson = InThirdPerson();
			const bool queued = papyrus::CallStatic("CharGen", "SaveCharacter",
				[a_ticket, a_id, name, start, thirdPerson](const RE::BSScript::Variable&) {
					const auto path = paths::PresetPath(a_id);
					std::error_code ec;
					const auto size = std::filesystem::file_size(path, ec);
					const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
					if (ec) {
						logger::error("Sidecar: CharGen.SaveCharacter('{}') finished but {} is missing ({})", name, text::PathUtf8(path), ec.message());
					} else {
						logger::info("Sidecar: preset '{}' exported ({} bytes, {:.0f} ms after queueing)", name, size, ms);
					}
					// RaceMenu's own tint export (a task it queues) never wrote a
					// file on the user's setup, so export it ourselves too.
					QueueTask([a_ticket, name]() {
						if (StillCurrent(a_ticket)) {
							auto* player = RE::PlayerCharacter::GetSingleton();
							appearance::CheckPreset(std::format("in game, just exported {}", name), a_ticket.characterId,
								player ? player->GetActorBase() : nullptr);
							tint::ExportFromPlayer(a_ticket.characterId);
						} else {
							logger::info("Sidecar: skipping the tint export for {}: the player has changed", FormatCharacterId(a_ticket.characterId));
						}
					});
					RunAfter(std::chrono::seconds(3), [a_ticket, thirdPerson]() { CheckTint(a_ticket, thirdPerson); });
				},
				RE::BSFixedString(name));
			if (!queued) {
				logger::error("Sidecar: could not call CharGen.SaveCharacter - is RaceMenu installed?");
				std::scoped_lock lock(g_lock);
				g_lastExport.erase(a_id);
			}
		}
	}

	std::optional<Sidecar> Read(std::uint32_t a_characterId)
	{
		const auto path = paths::SidecarPath(a_characterId);
		if (!path || !std::filesystem::exists(*path)) {
			return std::nullopt;
		}
		try {
			std::ifstream in(*path, std::ios::binary);
			return FromJson(json::parse(in));
		} catch (const std::exception& e) {
			logger::error("Sidecar: failed to parse {}: {}", text::PathUtf8(*path), e.what());
			return std::nullopt;
		}
	}

	namespace
	{
		// Capture on load: once the loading screen is gone and the player's 3D
		// is in (RaceMenu's export reads it). A newer load cancels it.
		constexpr auto kLoadCaptureDelay = std::chrono::seconds(3);
		constexpr auto kLoadCaptureRetry = std::chrono::seconds(2);
		constexpr int  kLoadCaptureTries = 15;

		std::string   g_loadingSave;  // from PreLoadGame, without .ess
		std::uint32_t g_loadEpoch{ 0 };

		// Writes the sidecar for the current player and exports their preset
		// (throttled). a_why: "save" or "load".
		void Capture(std::string_view a_saveName, std::string_view a_why)
		{
			if (lobby::GetState() != lobby::State::kIdle) {
				logger::info("Sidecar: {} of '{}' in the lobby, not capturing", a_why, a_saveName);
				return;
			}

			const auto parts = ParseSaveName(a_saveName);
			if (!parts) {
				logger::warn("Sidecar: save '{}' has no character ID in its name, skipping", a_saveName);
				return;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			auto* npc = player ? player->GetActorBase() : nullptr;
			if (!npc) {
				logger::error("Sidecar: no player/actor base at {} time", a_why);
				return;
			}

			const auto start = std::chrono::steady_clock::now();
			Sidecar s;
			s.characterId = parts->characterId;
			// Engine text is in the game's code page; JSON needs UTF-8 (a
			// name like "Björn" made dump() throw and crash the game).
			s.name = text::ToUtf8(player->GetName() ? player->GetName() : "");
			if (auto race = MakeFormRef(player->GetRace())) {
				s.race = *race;
			} else {
				logger::warn("Sidecar: player race has no source plugin");
			}
			s.female = npc->IsFemale();
			s.worn = WornItems(player);
			s.spells = HandSpells(player);

			const auto id = parts->characterId;
			std::optional<ExportTracker::Ticket> ticket;
			{
				// A save or a load capture of this character: they're the player.
				std::scoped_lock lock(g_lock);
				g_exports.OnSave(id);
				ticket = g_exports.Begin(id);
			}
			// The real character as the game has them, next to the saved look
			// the select screen will use (checked again once a new export lands).
			const auto who = std::format("in game, {} '{}' on {}", FormatCharacterId(id), s.name, a_why);
			appearance::LogRecord(who, npc);
			appearance::Log3D(who, player);
			if (ticket && ShouldExportPreset(id)) {
				ExportPreset(*ticket);
			} else {
				appearance::CheckPreset(who, id, npc);
			}
			s.preset = paths::PresetName(id);

			const auto path = paths::SidecarPath(id);
			if (!path) {
				logger::error("Sidecar: cannot resolve sidecar path");
				return;
			}
			std::string error;
			// replace: a stray invalid byte becomes U+FFFD rather than an exception.
			const bool  ok = files::WriteAtomic(*path, ToJson(s).dump(2, ' ', false, nlohmann::json::error_handler_t::replace), error);
			const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (ok) {
				logger::info("Sidecar: wrote {} on {} for {} '{}' ({} worn, {} spells, {:.1f} ms)", text::PathUtf8(path->filename()), a_why,
					FormatCharacterId(id), s.name, s.worn.size(), s.spells.size(), ms);
				for (const auto& w : s.worn) {
					logger::debug("  worn: {} {:08X} '{}'{}", w.plugin, w.localId, w.name, w.hand.empty() ? "" : " (" + w.hand + " hand)");
				}
				for (const auto& sp : s.spells) {
					logger::debug("  spell: {} {:08X} '{}' ({} hand)", sp.plugin, sp.localId, sp.name, sp.hand);
				}
			} else {
				logger::error("Sidecar: failed to write {}: {}", text::PathUtf8(*path), error);
			}
		}

		void TryLoadCapture(std::uint32_t a_epoch, std::string a_saveName, int a_try)
		{
			if (a_epoch != g_loadEpoch) {
				return;
			}
			auto*      player = RE::PlayerCharacter::GetSingleton();
			auto*      ui = RE::UI::GetSingleton();
			const bool loading = ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
			if (lobby::GetState() != lobby::State::kIdle || !player || !player->Is3DLoaded() || loading) {
				if (a_try + 1 >= kLoadCaptureTries) {
					logger::warn("Sidecar: gave up capturing '{}' after its load (lobby {}, 3D={}, loading screen={})", a_saveName,
						lobby::StateName(lobby::GetState()), player && player->Is3DLoaded(), loading);
					return;
				}
				RunAfter(kLoadCaptureRetry, [a_epoch, name = std::move(a_saveName), a_try]() mutable { TryLoadCapture(a_epoch, std::move(name), a_try + 1); });
				return;
			}
			Capture(a_saveName, "load");
		}
	}

	void OnSaveGame(std::string_view a_saveName)
	{
		Capture(a_saveName, "save");
	}

	void OnPostLoadGame(bool a_success)
	{
		if (!a_success || g_loadingSave.empty()) {
			return;
		}
		const auto epoch = g_loadEpoch;
		RunAfter(kLoadCaptureDelay, [epoch, name = g_loadingSave]() mutable { TryLoadCapture(epoch, std::move(name), 0); });
	}

	void OnPreLoadGame(std::string_view a_saveName)
	{
		++g_loadEpoch;
		g_loadingSave = std::string(a_saveName);
		if (g_loadingSave.ends_with(".ess")) {
			g_loadingSave.resize(g_loadingSave.size() - 4);
		}
		const auto       parts = ParseSaveName(g_loadingSave);
		std::scoped_lock lock(g_lock);
		if (g_exports.HasPending()) {
			logger::debug("Sidecar: dropping pending tint export retries (game loading)");
		}
		g_exports.OnLoad(parts ? std::optional(parts->characterId) : std::nullopt);
	}

	void OnMainMenuOpened()
	{
		std::scoped_lock lock(g_lock);
		g_exports.Forget();
	}

	void OnRaceMenuClosed()
	{
		std::scoped_lock lock(g_lock);
		g_raceMenuDirty = true;
		logger::info("Sidecar: RaceMenu closed, preset will be re-exported on next save");
	}
}
