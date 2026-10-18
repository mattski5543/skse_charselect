// Key maps, and what each press does in the lobby (see InputMap.h).

#include "core/InputMap.h"

namespace charselect::input
{
	using namespace key;

	std::optional<Nav> MapNav(Device a_device, std::uint32_t a_key)
	{
		switch (a_device) {
		case Device::kKeyboard:
			switch (a_key) {
			case kLeft:
			case kA:
			case kPgUp:
				return Nav::kLeft;
			case kRight:
			case kD:
			case kPgDn:
				return Nav::kRight;
			case kEnter:
			case kE:
				return Nav::kConfirm;
			case kS:
				return Nav::kSaves;
			default:
				return std::nullopt;
			}
		case Device::kGamepad:
			switch (a_key) {
			case kPadLeft:
				return Nav::kLeft;
			case kPadRight:
				return Nav::kRight;
			case kPadA:
				return Nav::kConfirm;
			case kPadX:
				return Nav::kSaves;
			default:
				return std::nullopt;
			}
		default:
			return std::nullopt;
		}
	}

	std::optional<EditKey> MapEditKey(Device a_device, std::uint32_t a_key)
	{
		if (a_device == Device::kGamepad) {
			switch (a_key) {
			case kPadUp:
				return EditKey::kForward;
			case kPadDown:
				return EditKey::kBack;
			case kPadLeft:
				return EditKey::kLeft;
			case kPadRight:
				return EditKey::kRight;
			case kPadA:
				return EditKey::kSaveClose;
			case kPadB:
				return EditKey::kCancel;
			default:
				return std::nullopt;
			}
		}
		if (a_device != Device::kKeyboard) {
			return std::nullopt;
		}
		switch (a_key) {
		case kUp:
		case kW:
			return EditKey::kForward;
		case kDown:
		case kS:
			return EditKey::kBack;
		case kLeft:
		case kA:
			return EditKey::kLeft;
		case kRight:
		case kD:
			return EditKey::kRight;
		case kR:
			return EditKey::kRaise;
		case kF:
			return EditKey::kLower;
		case kQ:
			return EditKey::kTurnLeft;
		case kE:
			return EditKey::kTurnRight;
		case kT:
			return EditKey::kTiltUp;
		case kG:
			return EditKey::kTiltDown;
		case kTab:
			return EditKey::kNextTarget;
		case kKey0:
			return EditKey::kCamera;
		case kEnter:
			return EditKey::kSaveClose;
		case kEsc:
			return EditKey::kCancel;
		default:
			return std::nullopt;
		}
	}

	bool IsRepeatable(EditKey a_key)
	{
		switch (a_key) {
		case EditKey::kNextTarget:
		case EditKey::kCamera:
		case EditKey::kSaveClose:
		case EditKey::kCancel:
			return false;
		default:
			return true;
		}
	}

	Action Route(const Context& a_ctx, Device a_device, std::uint32_t a_key)
	{
		using Kind = Action::Kind;
		const bool keyboard = a_device == Device::kKeyboard;

		if (keyboard && a_key == a_ctx.lobbyHotkey) {
			if (a_ctx.state == lobby::State::kIdle && a_ctx.atMainMenu && !a_ctx.otherMenuOnTop) {
				return { Kind::kEnterLobby };
			}
			if (a_ctx.state == lobby::State::kActive && a_ctx.selectMenuOpen && !a_ctx.otherMenuOnTop) {
				return { Kind::kSummon };
			}
			return {};
		}
		// Only our own menu, on top, takes the lobby's keys: typing in the
		// console mapped letters and Enter to PLAY.
		if (a_ctx.state != lobby::State::kActive || !a_ctx.selectMenuOpen || a_ctx.otherMenuOnTop) {
			return {};
		}
		if (keyboard && a_key == kF8) {
			return { Kind::kToggleEditor };
		}
		if (a_device == Device::kMouse && a_key == kMouseLeft) {
			return { Kind::kClick };
		}
		if (keyboard && a_key >= kKey1 && a_key <= kKey9) {
			return { Kind::kSelectSlot, a_key - kKey1 };
		}
		if (a_ctx.editorOpen) {
			if (const auto edit = MapEditKey(a_device, a_key)) {
				Action action{ Kind::kEdit };
				action.edit = *edit;
				return action;
			}
			return {};
		}
		if (const auto nav = MapNav(a_device, a_key)) {
			Action action{ Kind::kNav };
			action.nav = *nav;
			return action;
		}
		return {};
	}

	bool RepeatReachesEditor(const Context& a_ctx)
	{
		return a_ctx.state == lobby::State::kActive && a_ctx.selectMenuOpen && a_ctx.editorOpen && !a_ctx.otherMenuOnTop;
	}
}
