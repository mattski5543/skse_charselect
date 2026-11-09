#pragma once

// The lobby's decisions, free of the game so they can be unit tested: which
// state a load event leads to, when message boxes are closed, when the main
// menu is replaced. Lobby.cpp and Frontend.cpp act on these answers.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
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
	// Mods' message boxes in the lobby's new game (setup pop-ups, Wintersun's
	// choose-a-god) are removed, behind the splash and on the select screen.
	// Kept: the Journal's (the missing-content prompt of a LOAD from it,
	// Quit's question), the lobby's own (a_own), and any once a load is under
	// way (PLAY's missing-content prompt).
	[[nodiscard]] bool ShouldDismissMessageBoxes(State a_state, bool a_revealed, bool a_journalOpen, bool a_own);

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
	// The watchdog after PLAY, a_timeout after the load was requested
	// (a_sameLoad: no other PLAY since).
	[[nodiscard]] LoadWatch CheckLoadStart(bool a_sameLoad, State a_state, bool a_loadStarted, bool a_promptOrLoadingOpen);

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
		bool  replaceAtStartup{ true };  // CharSelect.ini bReplaceAtStartup
		bool  startupMenu{ false };      // the main menu the game started on (not back from a game)
		bool  raceMenuLoaded{ true };    // skee64.dll is loaded
		bool  classicThisVisit{ false };  // the player picked MAIN MENU on the select screen
		bool  actionPending{ false };     // NEW GAME or MAIN MENU on its way through the main menu
		State state{ State::kIdle };
		bool  loadFailedRecently{ false };
		bool  menuAvailable{ true };  // the select menu's SWF loaded
	};
	// The main menu opening becomes the select screen. (F7 opens it whatever
	// these say.)
	[[nodiscard]] bool ShouldReplaceMainMenu(const MainMenuContext& a_ctx);

	// Which of a_count locations (sLobbyCell's list) this lobby opens in, from
	// a random number: any of them, but not a_last again when there are others.
	[[nodiscard]] std::size_t PickLobbyCell(std::size_t a_count, std::optional<std::size_t> a_last, std::uint32_t a_random);

	// The lobby is seen through its player's eyes, with the free camera held
	// there (camera mods leave it alone). Where the eyes are, from the
	// player's feet, is read from the camera or the head once the player is
	// in place; a reading that can't be the eyes (the 3D not ready yet) gives
	// way to kDefaultEyeHeight straight up.
	inline constexpr float kAtHeadDistance = 60.0f;     // game units: a camera this close to the head is at the eyes
	inline constexpr float kDefaultEyeHeight = 120.0f;  // about a Nord's
	[[nodiscard]] bool LooksLikeEyes(float a_sideways, float a_up);

	// Taking the camera back, until another mod keeps switching it away:
	// more than kMaxFixes within kWindow, and the lobby stops fighting it.
	class CameraFixLimiter
	{
	public:
		using Clock = std::chrono::steady_clock;
		static constexpr int  kMaxFixes = 5;
		static constexpr auto kWindow = std::chrono::seconds(3);

		// A fix about to be made: false once this has given up.
		[[nodiscard]] bool Allow(Clock::time_point a_now);
		[[nodiscard]] bool GaveUp() const { return _gaveUp; }
		void               Reset() { *this = {}; }

	private:
		std::deque<Clock::time_point> _recent;
		bool                          _gaveUp{ false };
	};
}
