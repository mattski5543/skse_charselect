#pragma once

// What a key or button press does in the lobby, free of the game so it can
// be unit tested. Events.cpp reads the engine's input events, asks Route()
// and queues the answer for the select menu.

#include "core/LobbyRules.h"

#include <cstdint>
#include <optional>

namespace charselect::input
{
	enum class Device
	{
		kKeyboard,
		kMouse,
		kGamepad,
		kOther,
	};

	// Key codes as the engine reports them.
	namespace key
	{
		// DirectInput scan codes
		constexpr std::uint32_t kEsc = 0x01, kEnter = 0x1C, kE = 0x12, kS = 0x1F, kA = 0x1E, kD = 0x20, kW = 0x11;
		constexpr std::uint32_t kUp = 0xC8, kDown = 0xD0, kLeft = 0xCB, kRight = 0xCD, kPgUp = 0xC9, kPgDn = 0xD1;
		constexpr std::uint32_t kKey1 = 0x02, kKey9 = 0x0A;
		constexpr std::uint32_t kF8 = 0x42;  // layout editor
		constexpr std::uint32_t kQ = 0x10, kR = 0x13, kT = 0x14, kF = 0x21, kG = 0x22, kTab = 0x0F, kKey0 = 0x0B;  // editor panel
		// Mouse button codes
		constexpr std::uint32_t kMouseLeft = 0, kWheelUp = 8, kWheelDown = 9;
		// Gamepad (XInput) codes
		constexpr std::uint32_t kPadUp = 0x0001, kPadDown = 0x0002, kPadLeft = 0x0004, kPadRight = 0x0008;
		constexpr std::uint32_t kPadA = 0x1000, kPadB = 0x2000, kPadX = 0x4000;
	}

	// Select screen navigation.
	enum class Nav
	{
		kLeft,
		kRight,
		kUp,      // between the characters and the NEW GAME / LOAD / MAIN MENU / QUIT list
		kDown,
		kBack,    // out of that list, back to the characters
		kConfirm,
		kSaves,   // opens the vanilla Load list
	};

	// Layout editor keys.
	enum class EditKey
	{
		kForward,
		kBack,
		kLeft,
		kRight,
		kRaise,
		kLower,
		kTurnLeft,
		kTurnRight,
		kTiltUp,
		kTiltDown,
		kNextTarget,
		kCamera,
		kSaveClose,
		kCancel,
	};

	[[nodiscard]] std::optional<Nav>     MapNav(Device a_device, std::uint32_t a_key);
	[[nodiscard]] std::optional<EditKey> MapEditKey(Device a_device, std::uint32_t a_key);
	// Held editor keys that repeat (moving, turning); not one-shots.
	[[nodiscard]] bool IsRepeatable(EditKey a_key);

	struct Context
	{
		lobby::State  state{ lobby::State::kIdle };
		bool          selectMenuOpen{ false };
		bool          revealed{ false };    // the select screen has replaced the loading splash
		bool          menuFading{ false };  // fading out to leave (ENTER, NEW GAME, MAIN MENU, QUIT)
		bool          editorOpen{ false };
		bool          atMainMenu{ false };
		// A menu that takes typing or clicks is over ours: the console, the
		// Journal, a message box, RaceMenu.
		bool          otherMenuOnTop{ false };
		std::uint32_t lobbyHotkey{ 0x41 };  // F7
	};

	struct Action
	{
		enum class Kind
		{
			kNone,
			kEnterLobby,    // the hotkey at the main menu
			kSummon,        // the hotkey in the lobby: re-run the layout
			kToggleEditor,  // F8: open the editor, or save and close it
			kClick,         // left mouse button
			kSelectSlot,    // 1-9: slot
			kEdit,          // an editor key: edit
			kNav,           // select screen navigation: nav
		};
		Kind        kind{ Kind::kNone };
		std::size_t slot{ 0 };
		EditKey     edit{ EditKey::kForward };
		Nav         nav{ Nav::kLeft };
	};

	// What a fresh press (not a held repeat) does.
	[[nodiscard]] Action Route(const Context& a_ctx, Device a_device, std::uint32_t a_key);
	// Whether a held key's repeat reaches the editor.
	[[nodiscard]] bool RepeatReachesEditor(const Context& a_ctx);
}
