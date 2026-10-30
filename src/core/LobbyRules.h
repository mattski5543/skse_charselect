#pragma once

// The lobby's decisions, free of the game so they can be unit tested: which
// state a load event leads to, when message boxes are closed, when the main
// menu is replaced. Lobby.cpp and Frontend.cpp act on these answers.

#include <chrono>
#include <cstddef>
#include <optional>
#include <string_view>

namespace charselect::lobby
{
	enum class State
	{
		kIdle,         // normal play / main menu
		kEntering,     // coc issued, waiting for the player to settle in the cell
		kActive,       // in the lobby, actors spawned
		kLoadingSave,  // a save load was requested from the lobby
		kLeaving,      // its game is ending (back to the main menu, or quitting): nothing acts on it
	};

	[[nodiscard]] std::string_view StateName(State a_state);
}

namespace charselect::lobby::rules
{
	// Mod message boxes are closed only while the lobby starts (behind the
	// splash), when mods' setup pop-ups appear. Once the select screen shows,
	// every message box reaches the player, above all the game's
	// missing-content prompt.
	[[nodiscard]] bool ShouldDismissMessageBoxes(State a_state, bool a_revealed);

	// RequestEnter: only from a game with no lobby.
	[[nodiscard]] bool CanEnter(State a_state);

	// PLAY: a lobby that's up, a slot that exists, a character with saves.
	[[nodiscard]] bool CanPlay(State a_state, std::size_t a_slot, std::size_t a_slotCount, bool a_hasSaves);

	// LoadFile: only from a lobby that's up.
	[[nodiscard]] bool CanStartLoad(State a_state);

	enum class PreLoad
	{
		kNone,
		kStopLobby,        // a LOAD from the Journal: into kLoadingSave as PLAY does
		kAbandonEntering,  // a load (console, a mod) while the lobby is still starting: forget the lobby
	};
	// SKSE PreLoadGame. Teardown (saving and HUD back on) happens whatever
	// this says.
	[[nodiscard]] PreLoad OnPreLoadGame(State a_state);

	enum class PostLoad
	{
		kIgnore,        // not ours (no lobby)
		kWarnEntering,  // a load finished while the lobby was starting: log only
		kResume,        // a load from the lobby didn't happen: back to the lobby
		kStay,          // a failed load reported while the lobby is up: nothing changes
		kLeave,         // the player is in their game now: forget the lobby
	};
	// SKSE PostLoadGame.
	[[nodiscard]] PostLoad OnPostLoadGame(State a_state, bool a_success);

	// The main menu came back: any lobby game is over.
	[[nodiscard]] bool ShouldResetOnMainMenu(State a_state);

	// The Journal closed: bring the select screen back if a cancelled LOAD
	// from it had hidden ours.
	[[nodiscard]] bool ShouldShowSelectOnJournalClose(State a_state, bool a_revealed, bool a_editorOpen, bool a_selectMenuOpen);

	enum class LoadWatch
	{
		kStop,     // the load started, the lobby moved on, or a newer lobby exists
		kRecheck,  // a prompt or the loading screen is up: still under way
		kResume,   // nothing happened: back to the select screen
	};
	// The watchdog after PLAY, a_timeout after the load was requested.
	[[nodiscard]] LoadWatch CheckLoadStart(bool a_sameGeneration, State a_state, bool a_loadStarted, bool a_promptOrLoadingOpen);

	// Remembers when the last load from the lobby started. A main menu soon
	// after means the game gave up on it, and re-entering the lobby from
	// there would loop.
	class LoadFailureTracker
	{
	public:
		using Clock = std::chrono::steady_clock;
		static constexpr auto kWindow = std::chrono::seconds(60);

		void               Started(Clock::time_point a_now) { _startedAt = a_now; }
		void               Clear() { _startedAt.reset(); }
		[[nodiscard]] bool FailedRecently(Clock::time_point a_now) const { return _startedAt && a_now - *_startedAt < kWindow; }

	private:
		std::optional<Clock::time_point> _startedAt;
	};

	struct MainMenuContext
	{
		bool  replaceMainMenu{ true };   // CharSelect.ini bReplaceMainMenu
		bool  raceMenuLoaded{ true };    // skee64.dll is loaded
		bool  classicThisVisit{ false };  // the player picked MAIN MENU on the select screen
		bool  actionPending{ false };     // NEW GAME or MAIN MENU on its way through the main menu
		State state{ State::kIdle };
		bool  loadFailedRecently{ false };
		bool  menuAvailable{ true };  // the select menu's SWF loaded
	};
	// The main menu opening becomes the select screen.
	[[nodiscard]] bool ShouldReplaceMainMenu(const MainMenuContext& a_ctx);
}
