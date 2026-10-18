#pragma once

// Replaces the vanilla main menu with the character select flow.
//
// When the main menu opens, its movie is hidden, the select menu shows a
// splash, and the lobby starts. The select screen only does what vanilla
// can't (pick a character, Play); everything else is vanilla's own UI:
//   New Game, Main Menu - back to the main menu with a pending action,
//                         performed through its GameDelegate callbacks
//   Load                - the Journal's load page, opened over the lobby and
//                         switched to its list of all characters
// (No Settings: settings are often per save, and here they'd be the lobby's.)

namespace charselect::frontend
{
	enum class Action
	{
		kNone,
		kClassicMenu,  // just show the vanilla main menu
		kNewGame,
	};


	void Init();  // registers the lobby handlers; call at DataLoaded

	// From the menu-open event itself, before the vanilla menu draws: hides
	// it and shows the splash if the lobby is going to replace it.
	void CoverMainMenu();
	void OnMainMenuOpened();  // as a task, a moment later
	void OnMainMenuClosed();

	// From the vanilla main menu (hotkey): enter the lobby on demand.
	void EnterFromHotkey();

	// The select menu's SWF didn't load: fall back to the main menu.
	void OnSelectMenuUnavailable();

	// Select menu buttons.
	void ReturnToMainMenu(Action a_action);
	void OpenLoadMenu();
	void Quit();

	[[nodiscard]] std::string_view ActionName(Action a_action);
}
