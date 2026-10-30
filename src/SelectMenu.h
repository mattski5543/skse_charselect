#pragma once

#include "core/InputMap.h"

// The select screen: a Scaleform menu whose UI is built at runtime from C++
// through the GFx API on an empty SWF (Interface\CharSelectMenu.swf), so no
// Flash toolchain is needed.
//
// Three modes:
//   Splash - black, spinner and progress bottom right (covers the main menu and
//            the lobby being built)
//   Select - transparent over the lobby: name plates over the 3D characters,
//            the chosen character and Play; New Game / Load / Main Menu hand over
//            to vanilla's own menus
//   Edit   - the layout editor panel on the right (F8): pick the camera or a
//            character, move it with arrows, save
//
// Input arrives from the global input sink (Events.cpp) as queued clicks and
// navigation, and is processed in AdvanceMovie on the UI thread, where the
// layout and hit regions live. Game actions are handed on as SKSE tasks.

namespace charselect
{
	class SelectMenu : public RE::IMenu
	{
	public:
		static constexpr std::string_view MENU_NAME = "CharSelectMenu";
		static constexpr std::string_view FILE_NAME = "CharSelectMenu";

		// Select screen navigation and layout editor keys (core/InputMap.h).
		using Nav = input::Nav;
		using EditKey = input::EditKey;

		static void Register();
		static void ShowSplash(std::string_view a_text);
		static void SetSplashText(std::string_view a_text);  // progress; only while the splash is the mode
		static void SetGamepad(bool a_gamepad);  // which buttons the legends show; any thread
		static void ShowSelect();
		static void ShowEdit();
		static void Hide();
		// The menu's SWF failed to load this session: there is no select screen.
		[[nodiscard]] static bool MovieFailed();
		// Fading to black to leave (ENTER, NEW GAME, MAIN MENU, QUIT), or black after it.
		[[nodiscard]] static bool IsFading();
		[[nodiscard]] static bool IsOpen();

		static void QueueClick(bool a_repeat = false);  // a_repeat: mouse held in the editor
		static void QueueNav(Nav a_nav);
		static void QueueSelect(std::size_t a_slot);  // in the editor: edit that slot
		static void QueueEdit(EditKey a_key);

		SelectMenu();
		~SelectMenu() override;

		RE::UI_MESSAGE_RESULTS ProcessMessage(RE::UIMessage& a_message) override;
		void                   AdvanceMovie(float a_interval, std::uint32_t a_currentTime) override;

	private:
		RE::UI_MESSAGE_RESULTS HandleMessage(RE::UIMessage& a_message);
		void                   Advance(float a_interval, std::uint32_t a_currentTime);
	};
}
