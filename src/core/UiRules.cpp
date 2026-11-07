// Select screen helpers (see UiRules.h).

#include "core/UiRules.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>

namespace charselect::ui
{
	std::string FriendlyDate(const std::string& a_when)
	{
		static constexpr std::array<const char*, 12> kMonths{ "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
		if (a_when.size() < 10) {
			return a_when;
		}
		const int month = std::atoi(a_when.substr(5, 2).c_str());
		const int day = std::atoi(a_when.substr(8, 2).c_str());
		if (month < 1 || month > 12) {
			return a_when;
		}
		return std::format("{} {} {}", day, kMonths[month - 1], a_when.substr(0, 4));
	}

	bool IsOtherModsHudMenu(std::string_view a_name, bool a_alwaysOpen)
	{
		// Every menu the game registers (CommonLibSSE-NG's MENU_NAMEs), and the
		// lobby's own.
		static constexpr std::array<std::string_view, 37> kNotOthers{ "BarterMenu", "Book Menu", "Console", "Console Native UI Menu",
			"ContainerMenu", "Crafting Menu", "Creation Club Menu", "Credits Menu", "Cursor Menu", "Dialogue Menu", "Fader Menu",
			"FavoritesMenu", "GiftMenu", "HUD Menu", "InventoryMenu", "Journal Menu", "Kinect Menu", "LevelUp Menu", "LoadWaitSpinner",
			"Loading Menu", "Lockpicking Menu", "MagicMenu", "Main Menu", "MapMenu", "MessageBoxMenu", "Mist Menu", "Mod Manager Menu",
			"RaceSex Menu", "SafeZoneMenu", "Sleep/Wait Menu", "StatsMenu", "TitleSequence Menu", "Training Menu", "Tutorial Menu",
			"TweenMenu", "WSActivateRollover", "CharSelectMenu" };
		return a_alwaysOpen && !a_name.empty() && std::ranges::find(kNotOthers, a_name) == kNotOthers.end();
	}

	std::optional<std::size_t> StepSelection(const std::vector<std::size_t>& a_order, std::size_t a_selected, int a_dir)
	{
		const auto it = std::ranges::find(a_order, a_selected);
		if (it == a_order.end()) {
			return std::nullopt;
		}
		const auto pos = static_cast<std::size_t>(it - a_order.begin());
		if (a_dir < 0 && pos > 0) {
			return a_order[pos - 1];
		}
		if (a_dir > 0 && pos + 1 < a_order.size()) {
			return a_order[pos + 1];
		}
		return std::nullopt;
	}

	bool KeyRepeat::Due(std::uint32_t a_button, float a_held)
	{
		auto& next = _next[a_button];
		if (a_held < kDelay) {
			next = kDelay;
			return false;
		}
		if (a_held >= next) {
			next = a_held + kRate;
			return true;
		}
		return false;
	}
}
