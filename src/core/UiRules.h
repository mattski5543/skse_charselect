#pragma once

// Select screen helpers, free of the game so they can be unit tested.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace charselect::ui
{
	// "2026-10-13 20:48:15" -> "13 Oct 2026"; anything else unchanged.
	[[nodiscard]] std::string FriendlyDate(const std::string& a_when);

	// Left/Right on the select screen: the slot next to a_selected in the
	// on-screen order a_order (a_dir -1 left, +1 right). nullopt at either
	// end (no wrap) or if a_selected isn't shown.
	[[nodiscard]] std::optional<std::size_t> StepSelection(const std::vector<std::size_t>& a_order, std::size_t a_selected, int a_dir);

	// Held-button repeat for the layout editor's arrows (the press itself is
	// handled by the caller): nothing until held for kDelay, then once every
	// kRate.
	class KeyRepeat
	{
	public:
		static constexpr float kDelay = 0.35f;
		static constexpr float kRate = 0.07f;

		// a_held: how long this button has been held (0 on the press itself).
		// True when a repeat is due now.
		[[nodiscard]] bool Due(std::uint32_t a_button, float a_held);
		// A fresh press of a_button.
		void Reset(std::uint32_t a_button) { _next.erase(a_button); }

	private:
		std::unordered_map<std::uint32_t, float> _next;
	};
}
