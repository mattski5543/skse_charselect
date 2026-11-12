// Keeping a character one character through RaceMenu (see CharacterId.h).

#include "CharacterId.h"

#include "Config.h"
#include "Lobby.h"
#include "SaveIndex.h"
#include "Util.h"
#include "core/CharacterId.h"

namespace charselect::character
{
	namespace
	{
		std::mutex                   g_lock;
		std::optional<std::uint32_t> g_known;    // under g_lock: the character in play, from a save's name
		std::optional<std::uint32_t> g_loading;  // under g_lock: the ID of the save loading

		std::optional<std::uint32_t> IdFromSaveName(std::string_view a_saveName)
		{
			if (a_saveName.ends_with(".ess")) {
				a_saveName.remove_suffix(4);
			}
			const auto parts = ParseSaveName(a_saveName);
			return parts ? std::optional(parts->characterId) : std::nullopt;
		}

		// The game's ID next to the one from save names, for the debug log.
		// After a load the game's is 0 until the first save.
		void LogIds(std::string_view a_when)
		{
			if (!spdlog::should_log(spdlog::level::debug)) {
				return;
			}
			auto*                        manager = RE::BGSSaveLoadManager::GetSingleton();
			std::optional<std::uint32_t> known;
			{
				std::scoped_lock lock(g_lock);
				known = g_known;
			}
			logger::debug("Character: {}: character {} (from save names), game's ID {}", a_when, known ? FormatCharacterId(*known) : "none",
				manager ? FormatCharacterId(manager->currentCharacterID) : "-");
		}

		// The save manager listens for RaceMenu's name being confirmed (as
		// RaceMenu closes, during play or a new game's character creation)
		// and gives the player a new character ID there. The game's handler
		// runs first; then, by character::IdToKeep, the character's ID (from
		// its save names) is put back, as the face-only menu (which asks no
		// name) leaves it. The ID only names and groups saves: the save's
		// contents don't change. Only replaced when the game's ID before was
		// the character's, or not set yet; never another ID.
		using NameSink = RE::BSTEventSink<RE::RaceSexMenuEvent::NameChangedEvent>;
		using NameChanged = RE::BSEventNotifyControl (*)(NameSink*, const RE::RaceSexMenuEvent::NameChangedEvent*,
			RE::BSTEventSource<RE::RaceSexMenuEvent::NameChangedEvent>*);
		NameChanged g_nameChanged{ nullptr };

		void KeepId(RE::BGSSaveLoadManager& a_manager, std::uint32_t a_before)
		{
			const auto                   after = a_manager.currentCharacterID;
			std::optional<std::uint32_t> known;
			{
				std::scoped_lock lock(g_lock);
				known = g_known;
			}
			logger::debug("Character: RaceMenu's name confirmed: game's ID {} -> {}, character {}", FormatCharacterId(a_before), FormatCharacterId(after),
				known ? FormatCharacterId(*known) : "none");
			const auto keep = IdToKeep(config::Get().keepCharacterAfterRaceMenu, lobby::GetState() == lobby::State::kIdle, known, a_before, after);
			if (!keep) {
				return;
			}
			a_manager.currentCharacterID = *keep;
			logger::info("Character: kept character {} after RaceMenu (the game had made it a new character, {})", FormatCharacterId(*keep),
				FormatCharacterId(after));
		}

		RE::BSEventNotifyControl OnNameChanged(NameSink* a_sink, const RE::RaceSexMenuEvent::NameChangedEvent* a_event,
			RE::BSTEventSource<RE::RaceSexMenuEvent::NameChangedEvent>* a_source)
		{
			auto*      manager = RE::BGSSaveLoadManager::GetSingleton();
			const auto before = manager ? manager->currentCharacterID : 0;
			const auto result = g_nameChanged(a_sink, a_event, a_source);
			if (manager) {
				Guarded("Character ID", [&]() { KeepId(*manager, before); });
			}
			return result;
		}
	}

	void InstallKeepIdHook()
	{
		// BGSSaveLoadManager's second vtable is its BSTEventSink<NameChangedEvent>'s;
		// entry 1 is ProcessEvent.
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BGSSaveLoadManager[1] };
		g_nameChanged = reinterpret_cast<NameChanged>(vtable.write_vfunc(0x1, &OnNameChanged));
		logger::info("Character: RaceMenu name hook installed (keep the character after RaceMenu: {})", config::Get().keepCharacterAfterRaceMenu);
	}

	void OnPreLoad(std::string_view a_saveName)
	{
		std::scoped_lock lock(g_lock);
		g_loading = IdFromSaveName(a_saveName);
	}

	void OnLoaded(bool a_success)
	{
		if (a_success) {
			std::scoped_lock lock(g_lock);
			g_known = std::exchange(g_loading, std::nullopt);
		}
		LogIds("loaded");
	}

	void OnSaved(std::string_view a_saveName)
	{
		if (const auto id = IdFromSaveName(a_saveName)) {
			std::scoped_lock lock(g_lock);
			g_known = id;
		}
		LogIds("saved");
	}

	void Forget()
	{
		std::scoped_lock lock(g_lock);
		g_known.reset();
		g_loading.reset();
	}
}
