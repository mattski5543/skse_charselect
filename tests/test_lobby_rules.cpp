// Tests for the lobby's decisions around loads, menus and message boxes.

#include "core/LobbyRules.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <optional>
#include <vector>

using namespace charselect::lobby;
using namespace charselect::lobby::rules;
using namespace std::chrono_literals;

TEST_CASE("StateName names every state", "[lobby]")
{
	CHECK(StateName(State::kIdle) == "Idle");
	CHECK(StateName(State::kEntering) == "Entering");
	CHECK(StateName(State::kActive) == "Active");
	CHECK(StateName(State::kLoadingSave) == "LoadingSave");
	CHECK(StateName(State::kLeaving) == "Leaving");
	CHECK(StateName(static_cast<State>(99)) == "?");
}

TEST_CASE("Mods' message boxes are closed in the lobby, but not the ones that matter", "[lobby]")
{
	SECTION("behind the splash")
	{
		CHECK(ShouldDismissMessageBoxes(State::kEntering, false, false, false));
		CHECK(ShouldDismissMessageBoxes(State::kEntering, true, false, false));
		CHECK(ShouldDismissMessageBoxes(State::kActive, false, false, false));
	}
	SECTION("on the select screen: mods' pop-ups")
	{
		CHECK(ShouldDismissMessageBoxes(State::kActive, true, false, false));
	}
	SECTION("on the select screen: the Journal's (the missing-content prompt, Quit) and the lobby's own get through")
	{
		CHECK_FALSE(ShouldDismissMessageBoxes(State::kActive, true, true, false));
		CHECK_FALSE(ShouldDismissMessageBoxes(State::kActive, true, false, true));
	}
	SECTION("never in the real game or during a load")
	{
		for (const bool revealed : { false, true }) {
			for (const bool journal : { false, true }) {
				CHECK_FALSE(ShouldDismissMessageBoxes(State::kIdle, revealed, journal, false));
				CHECK_FALSE(ShouldDismissMessageBoxes(State::kLoadingSave, revealed, journal, false));
				CHECK_FALSE(ShouldDismissMessageBoxes(State::kLeaving, revealed, journal, false));
			}
		}
	}
}

TEST_CASE("The lobby is entered only from a game without one", "[lobby]")
{
	CHECK(CanEnter(State::kIdle));
	CHECK_FALSE(CanEnter(State::kEntering));
	CHECK_FALSE(CanEnter(State::kActive));
	CHECK_FALSE(CanEnter(State::kLoadingSave));
}

TEST_CASE("PLAY needs an open lobby, a real slot and saves", "[lobby]")
{
	CHECK(CanPlay(State::kActive, 0, 3, true));
	CHECK(CanPlay(State::kActive, 2, 3, true));
	CHECK_FALSE(CanPlay(State::kActive, 3, 3, true));  // past the end
	CHECK_FALSE(CanPlay(State::kActive, 0, 0, true));  // no slots
	CHECK_FALSE(CanPlay(State::kActive, 0, 3, false));
	CHECK_FALSE(CanPlay(State::kActive, static_cast<std::size_t>(-1), 3, true));
	for (const auto state : { State::kIdle, State::kEntering, State::kLoadingSave }) {
		CHECK_FALSE(CanPlay(state, 0, 3, true));
	}
}

TEST_CASE("A load starts only from an open lobby (no double PLAY)", "[lobby]")
{
	CHECK(CanStartLoad(State::kActive));
	CHECK_FALSE(CanStartLoad(State::kLoadingSave));  // a second PLAY while the first loads
	CHECK_FALSE(CanStartLoad(State::kIdle));
	CHECK_FALSE(CanStartLoad(State::kEntering));
}

TEST_CASE("PreLoadGame", "[lobby]")
{
	SECTION("a LOAD from the Journal stops the lobby at once")
	{
		CHECK(OnPreLoadGame(State::kActive) == PreLoad::kStopLobby);
	}
	SECTION("a load while entering abandons the lobby, so it never stages in the player's game")
	{
		CHECK(OnPreLoadGame(State::kEntering) == PreLoad::kAbandonEntering);
	}
	SECTION("a load in the real game, or PLAY's own load, leaves the lobby alone")
	{
		CHECK(OnPreLoadGame(State::kIdle) == PreLoad::kNone);
		CHECK(OnPreLoadGame(State::kLoadingSave) == PreLoad::kNone);
	}
}

TEST_CASE("PostLoadGame", "[lobby]")
{
	SECTION("loads in the real game are not ours")
	{
		CHECK(OnPostLoadGame(State::kIdle, true) == PostLoad::kIgnore);
		CHECK(OnPostLoadGame(State::kIdle, false) == PostLoad::kIgnore);
	}
	SECTION("a load finishing while the lobby starts is only logged")
	{
		CHECK(OnPostLoadGame(State::kEntering, true) == PostLoad::kWarnEntering);
		CHECK(OnPostLoadGame(State::kEntering, false) == PostLoad::kWarnEntering);
	}
	SECTION("a cancelled or failed load from the lobby goes back to the lobby")
	{
		CHECK(OnPostLoadGame(State::kLoadingSave, false) == PostLoad::kResume);
	}
	SECTION("a successful load leaves the lobby")
	{
		CHECK(OnPostLoadGame(State::kLoadingSave, true) == PostLoad::kLeave);
		CHECK(OnPostLoadGame(State::kActive, true) == PostLoad::kLeave);
	}
	SECTION("a failed load reported while Active (no PreLoadGame seen) keeps the lobby")
	{
		// The lobby game is still running: leaving would forget it and turn
		// saving back on in it.
		CHECK(OnPostLoadGame(State::kActive, false) == PostLoad::kStay);
	}
}

TEST_CASE("PLAY followed by a cancelled prompt, then a retry", "[lobby][sequence]")
{
	// The state Lobby.cpp moves through for PLAY -> missing-content prompt ->
	// cancel -> PLAY again -> continue.
	State state = State::kActive;
	REQUIRE(CanStartLoad(state));
	state = State::kLoadingSave;                              // LoadFile
	CHECK(OnPreLoadGame(state) == PreLoad::kNone);            // PLAY's own PreLoadGame changes nothing
	REQUIRE(OnPostLoadGame(state, false) == PostLoad::kResume);
	state = State::kActive;                                   // ResumeLobby
	CHECK_FALSE(ShouldDismissMessageBoxes(State::kLoadingSave, true, false, false));  // the next PLAY's prompt is shown
	REQUIRE(CanStartLoad(state));
	state = State::kLoadingSave;
	REQUIRE(OnPostLoadGame(state, true) == PostLoad::kLeave);
	state = State::kIdle;
	CHECK(CanEnter(state));
}

TEST_CASE("LOAD from the Journal, cancelled, then the Journal closes", "[lobby][sequence]")
{
	State state = State::kActive;
	REQUIRE(OnPreLoadGame(state) == PreLoad::kStopLobby);
	state = State::kLoadingSave;
	REQUIRE(OnPostLoadGame(state, false) == PostLoad::kResume);
	state = State::kActive;
	// ResumeLobby leaves the select screen to OnJournalClosed while the Journal is up.
	CHECK(ShouldShowSelectOnJournalClose(state, true, false, false));
}

TEST_CASE("The main menu resets any lobby", "[lobby]")
{
	CHECK_FALSE(ShouldResetOnMainMenu(State::kIdle));
	CHECK(ShouldResetOnMainMenu(State::kEntering));
	CHECK(ShouldResetOnMainMenu(State::kActive));
	CHECK(ShouldResetOnMainMenu(State::kLoadingSave));
	CHECK(ShouldResetOnMainMenu(State::kLeaving));
}

TEST_CASE("A lobby being left takes nothing more until the main menu", "[lobby]")
{
	// MAIN MENU, NEW GAME or QUIT: its game is being torn down.
	CHECK_FALSE(CanEnter(State::kLeaving));
	CHECK_FALSE(CanPlay(State::kLeaving, 0, 3, true));
	CHECK_FALSE(CanStartLoad(State::kLeaving));
	CHECK_FALSE(ShouldDismissMessageBoxes(State::kLeaving, true, false, false));
	CHECK_FALSE(ShouldShowSelectOnJournalClose(State::kLeaving, true, false, false));
	CHECK(OnPreLoadGame(State::kLeaving) == PreLoad::kNone);
}

TEST_CASE("The select screen comes back when the Journal closes only in a shown, idle lobby", "[lobby]")
{
	CHECK(ShouldShowSelectOnJournalClose(State::kActive, true, false, false));
	CHECK_FALSE(ShouldShowSelectOnJournalClose(State::kActive, false, false, false));  // still behind the splash
	CHECK_FALSE(ShouldShowSelectOnJournalClose(State::kActive, true, true, false));    // editor panel open
	CHECK_FALSE(ShouldShowSelectOnJournalClose(State::kActive, true, false, true));    // already showing
	for (const auto state : { State::kIdle, State::kEntering, State::kLoadingSave }) {
		CHECK_FALSE(ShouldShowSelectOnJournalClose(state, true, false, false));
	}
}

TEST_CASE("The load watchdog", "[lobby]")
{
	SECTION("does nothing once the load started")
	{
		CHECK(CheckLoadStart(true, State::kLoadingSave, true, false) == LoadWatch::kStop);
	}
	SECTION("does nothing for an older lobby or a lobby that moved on")
	{
		CHECK(CheckLoadStart(false, State::kLoadingSave, false, false) == LoadWatch::kStop);
		for (const auto state : { State::kIdle, State::kEntering, State::kActive }) {
			CHECK(CheckLoadStart(true, state, false, false) == LoadWatch::kStop);
		}
	}
	SECTION("waits while the missing-content prompt or the loading screen is up")
	{
		CHECK(CheckLoadStart(true, State::kLoadingSave, false, true) == LoadWatch::kRecheck);
	}
	SECTION("brings the select screen back when nothing happened")
	{
		CHECK(CheckLoadStart(true, State::kLoadingSave, false, false) == LoadWatch::kResume);
	}
}

TEST_CASE("The loop breaker remembers a load attempt for 60 seconds", "[lobby]")
{
	using Clock = LoadFailureTracker::Clock;
	const auto        t0 = Clock::time_point{} + 1h;
	LoadFailureTracker tracker;

	CHECK_FALSE(tracker.FailedRecently(t0));
	tracker.Started(t0);
	CHECK(tracker.FailedRecently(t0));
	CHECK(tracker.FailedRecently(t0 + 59s));
	CHECK_FALSE(tracker.FailedRecently(t0 + 60s));
	CHECK_FALSE(tracker.FailedRecently(t0 + 10min));

	SECTION("a successful load clears it")
	{
		tracker.Clear();
		CHECK_FALSE(tracker.FailedRecently(t0 + 1s));
	}
	SECTION("a newer attempt restarts the window")
	{
		tracker.Started(t0 + 50s);
		CHECK(tracker.FailedRecently(t0 + 100s));
	}
}

TEST_CASE("The main menu is replaced only when everything allows it", "[lobby][frontend]")
{
	const MainMenuContext ok{};
	CHECK(ShouldReplaceMainMenu(ok));

	auto ctx = ok;
	SECTION("bReplaceMainMenu = 0")
	{
		ctx.replaceMainMenu = false;
	}
	SECTION("bReplaceAtStartup = 0, on the main menu the game started on")
	{
		ctx.replaceAtStartup = false;
		ctx.startupMenu = true;
	}
	SECTION("no RaceMenu")
	{
		ctx.raceMenuLoaded = false;
	}
	SECTION("the player chose MAIN MENU")
	{
		ctx.classicThisVisit = true;
	}
	SECTION("an action is on its way through the main menu")
	{
		ctx.actionPending = true;
	}
	SECTION("a lobby already exists")
	{
		ctx.state = GENERATE(State::kEntering, State::kActive, State::kLoadingSave);
	}
	SECTION("a load from the lobby just failed (loop breaker)")
	{
		ctx.loadFailedRecently = true;
	}
	SECTION("the select menu's SWF didn't load")
	{
		ctx.menuAvailable = false;
	}
	CHECK_FALSE(ShouldReplaceMainMenu(ctx));
}

TEST_CASE("A console load while the lobby starts", "[lobby][sequence]")
{
	// The player's game must not get the lobby staged into it.
	State state = State::kEntering;
	REQUIRE(OnPreLoadGame(state) == PreLoad::kAbandonEntering);
	state = State::kIdle;  // ForgetLobby
	CHECK(OnPostLoadGame(state, true) == PostLoad::kIgnore);
	CHECK_FALSE(ShouldDismissMessageBoxes(state, false, false, false));
	CHECK(CanEnter(state));
}

TEST_CASE("bReplaceAtStartup = 0 keeps only the startup main menu", "[lobby][frontend]")
{
	MainMenuContext ctx{};
	ctx.replaceAtStartup = false;
	ctx.startupMenu = true;
	CHECK_FALSE(ShouldReplaceMainMenu(ctx));  // the game has just started
	ctx.startupMenu = false;
	CHECK(ShouldReplaceMainMenu(ctx));  // back from a game
	ctx.replaceAtStartup = true;
	ctx.startupMenu = true;
	CHECK(ShouldReplaceMainMenu(ctx));  // the default replaces both
}

TEST_CASE("A random location is any of them, but not the last one again", "[lobby]")
{
	SECTION("one location: always it")
	{
		CHECK(PickLobbyCell(1, std::nullopt, 12345) == 0);
		CHECK(PickLobbyCell(1, 0, 12345) == 0);
		CHECK(PickLobbyCell(0, std::nullopt, 7) == 0);
	}
	SECTION("the first time, any of them")
	{
		std::vector<int> seen(5, 0);
		for (std::uint32_t r = 0; r < 50; ++r) {
			const auto pick = PickLobbyCell(5, std::nullopt, r);
			REQUIRE(pick < 5);
			++seen[pick];
		}
		for (const int n : seen) {
			CHECK(n > 0);
		}
	}
	SECTION("after that, every other one and never the last")
	{
		for (std::size_t last = 0; last < 5; ++last) {
			std::vector<int> seen(5, 0);
			for (std::uint32_t r = 0; r < 40; ++r) {
				const auto pick = PickLobbyCell(5, last, r);
				REQUIRE(pick < 5);
				CHECK(pick != last);
				++seen[pick];
			}
			for (std::size_t i = 0; i < 5; ++i) {
				INFO("last " << last << ", location " << i);
				CHECK((i == last) == (seen[i] == 0));
			}
		}
	}
	SECTION("a last one that's no longer in the list is ignored")
	{
		CHECK(PickLobbyCell(3, 7, 2) == 2);
	}
}

TEST_CASE("The lobby's view goes to the eyes only for a reading that can be the eyes", "[lobby][camera]")
{
	CHECK(LooksLikeEyes(0.0f, kDefaultEyeHeight));
	CHECK(LooksLikeEyes(10.0f, 125.0f));               // the first-person camera, a little forward
	CHECK(LooksLikeEyes(kAtHeadDistance, 125.0f));     // the edge counts
	CHECK_FALSE(LooksLikeEyes(200.0f, 125.0f));        // a third-person camera behind the player
	CHECK_FALSE(LooksLikeEyes(0.0f, 0.0f));            // the 3D not in place: the feet
	CHECK_FALSE(LooksLikeEyes(0.0f, 1000.0f));
}
