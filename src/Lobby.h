#pragma once

#include "SaveIndex.h"
#include "Sidecar.h"
#include "core/LobbyRules.h"

// Lobby flow:
//   Main menu (auto, or the hotkey)
//     -> "coc <lobby cell>" starts a throwaway game; saving is blocked in it
//     -> once the player has settled in the cell: hide other NPCs and the HUD,
//        park the player as a fixed camera, and spawn one actor per character
//        at the cell's layout spots (CharSelectLayouts.ini) with their
//        RaceMenu preset and worn gear
//     -> the select menu (SelectMenu) drives Play / Load / main menu actions
//     -> PLAY clears RaceMenu's lobby presets, then calls
//        BGSSaveLoadManager::Load; saving, controls and the HUD come back when
//        the load starts (PreLoadGame)

namespace charselect::lobby
{
	// State and StateName come from core/LobbyRules.h.

	struct SaveEntry
	{
		std::string   file;  // with .ess
		std::string   stem;
		std::uint32_t level{ 0 };
		std::string   location;
		std::string   when;  // "2026-10-13 20:48:15"
		std::string   saveType;
	};

	// What the menu shows for one character.
	struct SlotView
	{
		std::string            name;
		std::uint32_t          level{ 0 };
		std::string            race;
		std::string            location;
		std::string            lastPlayed;
		std::vector<SaveEntry> saves;  // newest first
		bool                   ready{ false };
		RE::ObjectRefHandle    ref;
		float                  lateral{ 0.0f };  // left (-) / right (+) of the camera, for card order
		RE::NiPoint3           pos;              // where the character stands (its spot)
		bool                   placeholder{ false };  // no captured appearance yet: a hooded stand-in, still playable
	};

	[[nodiscard]] State                 GetState();
	[[nodiscard]] std::vector<SlotView> GetSlots();
	// Cheap per-frame query for the menu: readiness and reference per slot.
	[[nodiscard]] std::vector<std::pair<bool, RE::ObjectRefHandle>> GetSlotStatus();

	// All of these run on the main thread.
	void RequestEnter();  // from the main menu; with no characters, the failure handler runs
	void OnLoadingMenuClosed();
	void OnPreLoadGame();
	// Any lobby game is over once the main menu is back (e.g. after New Game
	// or Classic Menu); forget it so saves are captured and the next lobby starts.
	void OnMainMenuOpened();
	// A load started from the lobby in the last minute hasn't succeeded: the main
	// menu showing now means the game gave up on it, so don't re-enter the lobby.
	[[nodiscard]] bool LoadFailedRecently();
	void               ClearLoadFailure();
	void OnPostLoadGame(bool a_success);
	void OnJournalClosed();  // brings the select screen back after a cancelled LOAD
	void OnNewGame();        // SKSE NewGame: blocks saving in the lobby's game at once

	// Records a form created at runtime in the lobby's game. After a load (and
	// when the main menu opens) the log says how many are still registered
	// with the engine, to find out whether the engine frees them. Main thread.
	void NoteRuntimeForm(const RE::TESForm* a_form, std::string_view a_kind);

	// Actions from the select menu.
	void Play(std::size_t a_slot);  // latest save
	void Summon();                                           // re-run the layout
	void Highlight(std::size_t a_slot);                      // move the light to this character

	// Layout editor panel (F8): pick the camera or a character and move it
	// with on-screen arrows; only what was changed is saved to Layout.ini.
	// Target 0 is the camera, target i+1 is slot i.
	struct EditTarget
	{
		std::string label;   // "Camera", "3  Ingrid"
		std::string kind;    // "view", "standing", "seated", ...
		std::string detail;  // position and heading
		std::string note;    // why it can't be moved, if it can't
		bool        movable{ true };
		bool        camera{ false };
		// Characters only: what the pose pickers show.
		std::string rest;      // "warm hands"
		std::string select;    // "draw weapon", "same as at rest"
		std::string seat;      // "Bench", "none", ...
		std::string poseNote;  // e.g. a seated pose with no seat

		bool operator==(const EditTarget&) const = default;
	};
	enum class EditField
	{
		kRest,
		kSelect,
		kSeat,
	};
	[[nodiscard]] bool                    IsEditorOpen();
	[[nodiscard]] std::vector<EditTarget> GetEditTargets();  // any thread
	void                                  OpenEditor();
	void                                  CloseEditor(bool a_save);  // false: revert unsaved changes
	// Moves a target relative to the camera's view: forward is away from the
	// camera, right is screen-right. Turn and tilt in radians (tilt: camera only).
	void EditNudge(std::size_t a_target, float a_forward, float a_right, float a_up, float a_turn, float a_tilt);
	void EditFocus(std::size_t a_target);  // the light follows the character being edited
	// Steps a character's rest pose, select pose or seat (a_dir +1/-1),
	// applied live and saved with the rest of the panel's changes.
	void EditCycle(std::size_t a_target, EditField a_field, int a_dir);
	void EditResetCell();                  // back to the built-in layout for this cell

	// Mod message boxes that pop up while the lobby starts are closed
	// automatically (the lobby is a throwaway game).
	[[nodiscard]] bool ShouldDismissMessageBoxes();

	// Undoes lobby-only changes before leaving the lobby another way
	// (back to the main menu). Safe to call more than once.
	void Teardown();

	// RaceMenu keeps the lobby faces mapped by actor base pointer and never
	// clears them on load (its Revert is empty); a recycled pointer in the next
	// game could pick one up. Clears them, then runs a_then on the main thread.
	void ClearPresets(std::function<void()> a_then = {});
	// Drops every lobby character's link to their furniture. Call before the
	// lobby's game is torn down (a load, or back to the main menu): a link
	// whose target is freed first crashes the engine. Main thread.
	void UnlinkSeats();

	// Called if the lobby could not be entered, so the frontend can fall back.
	void SetFailureHandler(std::function<void(std::string_view a_reason)> a_handler);
	// Called once characters are spawned and the select menu should appear.
	void SetReadyHandler(std::function<void()> a_handler);
}
