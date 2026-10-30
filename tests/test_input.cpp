// Tests for what each key press does in the lobby, including with the console open.

#include "core/InputMap.h"

#include <catch2/catch_test_macros.hpp>

using namespace charselect;
using namespace charselect::input;
using namespace charselect::input::key;
using Kind = Action::Kind;
using lobby::State;

namespace
{
	Context Lobby()
	{
		Context ctx;
		ctx.state = State::kActive;
		ctx.selectMenuOpen = true;
		ctx.revealed = true;
		return ctx;
	}

	Context Editor()
	{
		auto ctx = Lobby();
		ctx.editorOpen = true;
		return ctx;
	}

	Kind KindOf(const Context& a_ctx, Device a_device, std::uint32_t a_key) { return Route(a_ctx, a_device, a_key).kind; }
}

TEST_CASE("Typing in the console never reaches the lobby", "[input]")
{
	// The console stays enabled in the lobby; "help" + Enter mapped E and
	// Enter to PLAY.
	for (auto ctx : { Lobby(), Editor() }) {
		ctx.otherMenuOnTop = true;
		for (std::uint32_t code = 0; code < 0x100; ++code) {
			INFO("key " << code);
			CHECK(KindOf(ctx, Device::kKeyboard, code) == Kind::kNone);
		}
		CHECK(KindOf(ctx, Device::kMouse, kMouseLeft) == Kind::kNone);
		CHECK(KindOf(ctx, Device::kGamepad, kPadA) == Kind::kNone);
		CHECK_FALSE(RepeatReachesEditor(ctx));
	}
}

TEST_CASE("F8 only works on the select screen itself", "[input]")
{
	CHECK(KindOf(Lobby(), Device::kKeyboard, kF8) == Kind::kToggleEditor);
	CHECK(KindOf(Editor(), Device::kKeyboard, kF8) == Kind::kToggleEditor);

	auto behind = Lobby();
	behind.otherMenuOnTop = true;
	CHECK(KindOf(behind, Device::kKeyboard, kF8) == Kind::kNone);

	auto noMenu = Lobby();
	noMenu.selectMenuOpen = false;  // during a load
	CHECK(KindOf(noMenu, Device::kKeyboard, kF8) == Kind::kNone);
}

TEST_CASE("The lobby's keys wait for the select screen and stop while it fades out", "[input]")
{
	auto splash = Lobby();
	splash.revealed = false;  // "Loading characters"
	auto fading = Lobby();
	fading.menuFading = true;  // after ENTER, NEW GAME, MAIN MENU or QUIT
	for (const auto& ctx : { splash, fading }) {
		CHECK(KindOf(ctx, Device::kKeyboard, kF8) == Kind::kNone);
		CHECK(KindOf(ctx, Device::kKeyboard, 0x41) == Kind::kNone);
		CHECK(KindOf(ctx, Device::kKeyboard, kEnter) == Kind::kNone);
		CHECK(KindOf(ctx, Device::kGamepad, kPadA) == Kind::kNone);
	}
	auto editorSplash = Editor();
	editorSplash.revealed = false;
	CHECK_FALSE(RepeatReachesEditor(editorSplash));
}

TEST_CASE("Nothing reaches the lobby outside it", "[input]")
{
	for (const auto state : { State::kIdle, State::kEntering, State::kLoadingSave, State::kLeaving }) {
		auto ctx = Lobby();
		ctx.state = state;
		CHECK(KindOf(ctx, Device::kKeyboard, kEnter) == Kind::kNone);
		CHECK(KindOf(ctx, Device::kKeyboard, kF8) == Kind::kNone);
		CHECK(KindOf(ctx, Device::kMouse, kMouseLeft) == Kind::kNone);
		CHECK(KindOf(ctx, Device::kKeyboard, 0x41) == Kind::kNone);
	}
}

TEST_CASE("The lobby hotkey", "[input]")
{
	SECTION("at the main menu enters the lobby")
	{
		Context ctx;
		ctx.atMainMenu = true;
		CHECK(KindOf(ctx, Device::kKeyboard, 0x41) == Kind::kEnterLobby);
		ctx.otherMenuOnTop = true;  // a message box over the main menu
		CHECK(KindOf(ctx, Device::kKeyboard, 0x41) == Kind::kNone);
	}
	SECTION("in the real game does nothing")
	{
		Context ctx;
		CHECK(KindOf(ctx, Device::kKeyboard, 0x41) == Kind::kNone);
	}
	SECTION("in the lobby re-runs the layout")
	{
		CHECK(KindOf(Lobby(), Device::kKeyboard, 0x41) == Kind::kSummon);
	}
	SECTION("follows the INI")
	{
		Context ctx;
		ctx.atMainMenu = true;
		ctx.lobbyHotkey = 0x3B;
		CHECK(KindOf(ctx, Device::kKeyboard, 0x3B) == Kind::kEnterLobby);
		CHECK(KindOf(ctx, Device::kKeyboard, 0x41) == Kind::kNone);
	}
	SECTION("only from the keyboard")
	{
		Context ctx;
		ctx.atMainMenu = true;
		CHECK(KindOf(ctx, Device::kGamepad, 0x41) == Kind::kNone);
	}
}

TEST_CASE("Select screen keys", "[input]")
{
	const auto ctx = Lobby();
	CHECK(KindOf(ctx, Device::kMouse, kMouseLeft) == Kind::kClick);

	const auto slot = Route(ctx, Device::kKeyboard, kKey1 + 2);
	CHECK(slot.kind == Kind::kSelectSlot);
	CHECK(slot.slot == 2);
	CHECK(Route(ctx, Device::kKeyboard, kKey9).slot == 8);

	const auto nav = [&](Device a_device, std::uint32_t a_key) {
		const auto action = Route(ctx, a_device, a_key);
		REQUIRE(action.kind == Kind::kNav);
		return action.nav;
	};
	CHECK(nav(Device::kKeyboard, kEnter) == Nav::kConfirm);
	CHECK(nav(Device::kKeyboard, kE) == Nav::kConfirm);
	CHECK(nav(Device::kKeyboard, kA) == Nav::kLeft);
	CHECK(nav(Device::kKeyboard, kRight) == Nav::kRight);
	CHECK(nav(Device::kKeyboard, kS) == Nav::kSaves);
	CHECK(nav(Device::kKeyboard, kDown) == Nav::kDown);
	CHECK(nav(Device::kKeyboard, kUp) == Nav::kUp);
	CHECK(KindOf(ctx, Device::kKeyboard, kEsc) == Kind::kNone);  // Esc is the game's
	CHECK(nav(Device::kGamepad, kPadA) == Nav::kConfirm);
	CHECK(nav(Device::kGamepad, kPadX) == Nav::kSaves);
	// The gamepad reaches NEW GAME, LOAD, MAIN MENU and QUIT with the D-pad.
	CHECK(nav(Device::kGamepad, kPadDown) == Nav::kDown);
	CHECK(nav(Device::kGamepad, kPadUp) == Nav::kUp);
	CHECK(nav(Device::kGamepad, kPadB) == Nav::kBack);
	CHECK(KindOf(ctx, Device::kMouse, kWheelDown) == Kind::kNone);
	CHECK(KindOf(ctx, Device::kKeyboard, kQ) == Kind::kNone);
	CHECK(KindOf(ctx, Device::kOther, kEnter) == Kind::kNone);
}

TEST_CASE("Editor keys", "[input]")
{
	const auto ctx = Editor();
	const auto edit = [&](Device a_device, std::uint32_t a_key) {
		const auto action = Route(ctx, a_device, a_key);
		REQUIRE(action.kind == Kind::kEdit);
		return action.edit;
	};
	CHECK(edit(Device::kKeyboard, kW) == EditKey::kForward);
	CHECK(edit(Device::kKeyboard, kS) == EditKey::kBack);  // not "saves" in the editor
	CHECK(edit(Device::kKeyboard, kE) == EditKey::kTurnRight);  // not PLAY in the editor
	CHECK(edit(Device::kKeyboard, kEnter) == EditKey::kSaveClose);
	CHECK(edit(Device::kKeyboard, kEsc) == EditKey::kCancel);
	CHECK(edit(Device::kKeyboard, kKey0) == EditKey::kCamera);
	CHECK(edit(Device::kGamepad, kPadB) == EditKey::kCancel);
	CHECK(KindOf(ctx, Device::kMouse, kMouseLeft) == Kind::kClick);
	CHECK(Route(ctx, Device::kKeyboard, kKey1).kind == Kind::kSelectSlot);
	CHECK(KindOf(ctx, Device::kMouse, kWheelUp) == Kind::kNone);  // no navigation in the editor
}

TEST_CASE("Held keys repeat only movement in the editor", "[input]")
{
	CHECK(RepeatReachesEditor(Editor()));
	CHECK_FALSE(RepeatReachesEditor(Lobby()));
	CHECK(IsRepeatable(EditKey::kForward));
	CHECK(IsRepeatable(EditKey::kTurnLeft));
	CHECK(IsRepeatable(EditKey::kTiltDown));
	CHECK_FALSE(IsRepeatable(EditKey::kSaveClose));
	CHECK_FALSE(IsRepeatable(EditKey::kCancel));
	CHECK_FALSE(IsRepeatable(EditKey::kNextTarget));
	CHECK_FALSE(IsRepeatable(EditKey::kCamera));
}
