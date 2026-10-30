// The lobby's decisions (see LobbyRules.h).

#include "core/LobbyRules.h"

namespace charselect::lobby
{
	std::string_view StateName(State a_state)
	{
		using namespace std::literals;
		switch (a_state) {
		case State::kIdle:
			return "Idle"sv;
		case State::kEntering:
			return "Entering"sv;
		case State::kActive:
			return "Active"sv;
		case State::kLoadingSave:
			return "LoadingSave"sv;
		case State::kLeaving:
			return "Leaving"sv;
		}
		return "?"sv;
	}
}

namespace charselect::lobby::rules
{
	bool ShouldDismissMessageBoxes(State a_state, bool a_revealed)
	{
		return a_state == State::kEntering || (a_state == State::kActive && !a_revealed);
	}

	bool CanEnter(State a_state)
	{
		return a_state == State::kIdle;
	}

	bool CanPlay(State a_state, std::size_t a_slot, std::size_t a_slotCount, bool a_hasSaves)
	{
		return a_state == State::kActive && a_slot < a_slotCount && a_hasSaves;
	}

	bool CanStartLoad(State a_state)
	{
		return a_state == State::kActive;
	}

	PreLoad OnPreLoadGame(State a_state)
	{
		switch (a_state) {
		case State::kActive:
			return PreLoad::kStopLobby;
		case State::kEntering:
			return PreLoad::kAbandonEntering;
		default:
			return PreLoad::kNone;
		}
	}

	PostLoad OnPostLoadGame(State a_state, bool a_success)
	{
		if (a_state == State::kIdle) {
			return PostLoad::kIgnore;
		}
		if (a_state == State::kEntering) {
			return PostLoad::kWarnEntering;
		}
		// The missing-content prompt aborts the first attempt (if the player
		// continues, the game retries and a successful PostLoadGame follows),
		// or the load failed: either way still in the lobby. (Treating it as
		// having left would strand the player after a cancelled LOAD.)
		if (!a_success && a_state == State::kLoadingSave) {
			return PostLoad::kResume;
		}
		// No load started from the lobby (no PreLoadGame), and none happened:
		// the lobby game is still running, so it must not be forgotten.
		if (!a_success && a_state == State::kActive) {
			return PostLoad::kStay;
		}
		return PostLoad::kLeave;
	}

	bool ShouldResetOnMainMenu(State a_state)
	{
		return a_state != State::kIdle;
	}

	bool ShouldShowSelectOnJournalClose(State a_state, bool a_revealed, bool a_editorOpen, bool a_selectMenuOpen)
	{
		return a_state == State::kActive && a_revealed && !a_editorOpen && !a_selectMenuOpen;
	}

	LoadWatch CheckLoadStart(bool a_sameGeneration, State a_state, bool a_loadStarted, bool a_promptOrLoadingOpen)
	{
		if (!a_sameGeneration || a_state != State::kLoadingSave || a_loadStarted) {
			return LoadWatch::kStop;
		}
		return a_promptOrLoadingOpen ? LoadWatch::kRecheck : LoadWatch::kResume;
	}

	bool ShouldReplaceMainMenu(const MainMenuContext& a_ctx)
	{
		return a_ctx.replaceMainMenu && a_ctx.raceMenuLoaded && !a_ctx.classicThisVisit && !a_ctx.actionPending &&
		       a_ctx.state == State::kIdle && !a_ctx.loadFailedRecently && a_ctx.menuAvailable;
	}
}
