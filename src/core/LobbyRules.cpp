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
	bool ShouldDismissMessageBoxes(State a_state, bool a_revealed, bool a_journalOpen, bool a_own)
	{
		if (a_state == State::kEntering) {
			return true;
		}
		if (a_state != State::kActive) {
			return false;
		}
		return !a_revealed || (!a_journalOpen && !a_own);
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

	LoadWatch CheckLoadStart(bool a_sameLoad, State a_state, bool a_loadStarted, bool a_promptOrLoadingOpen)
	{
		if (!a_sameLoad || a_state != State::kLoadingSave || a_loadStarted) {
			return LoadWatch::kStop;
		}
		return a_promptOrLoadingOpen ? LoadWatch::kRecheck : LoadWatch::kResume;
	}

	bool ShouldReplaceMainMenu(const MainMenuContext& a_ctx)
	{
		return a_ctx.replaceMainMenu && (a_ctx.replaceAtStartup || !a_ctx.startupMenu) && a_ctx.raceMenuLoaded && !a_ctx.classicThisVisit && !a_ctx.actionPending &&
		       a_ctx.state == State::kIdle && !a_ctx.loadFailedRecently && a_ctx.menuAvailable;
	}
}

namespace charselect::lobby::rules
{
	std::size_t PickLobbyCell(std::size_t a_count, std::optional<std::size_t> a_last, std::uint32_t a_random)
	{
		if (a_count <= 1) {
			return 0;
		}
		if (!a_last || *a_last >= a_count) {
			return a_random % a_count;
		}
		const auto pick = a_random % (a_count - 1);  // among the others
		return pick >= *a_last ? pick + 1 : pick;
	}

	bool LooksLikeEyes(float a_sideways, float a_up)
	{
		// From a child's eyes to a giant race's.
		constexpr float kLowest = 40.0f;
		constexpr float kHighest = 250.0f;
		return a_sideways <= kAtHeadDistance && a_up >= kLowest && a_up <= kHighest;
	}

	bool CameraFixLimiter::Allow(Clock::time_point a_now)
	{
		if (_gaveUp) {
			return false;
		}
		while (!_recent.empty() && a_now - _recent.front() > kWindow) {
			_recent.pop_front();
		}
		if (static_cast<int>(_recent.size()) >= kMaxFixes) {
			_gaveUp = true;
			return false;
		}
		_recent.push_back(a_now);
		return true;
	}
}
