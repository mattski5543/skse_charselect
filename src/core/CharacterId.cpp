// Keeping a character's ID through RaceMenu (see CharacterId.h).

#include "core/CharacterId.h"

namespace charselect::character
{
	std::optional<std::uint32_t> IdToKeep(bool a_enabled, bool a_lobbyIdle, std::optional<std::uint32_t> a_known, std::uint32_t a_before, std::uint32_t a_after)
	{
		if (!a_enabled || !a_lobbyIdle || !a_known || *a_known == 0 || a_after == a_before || a_after == *a_known) {
			return std::nullopt;
		}
		if (a_before != *a_known && a_before != 0) {
			return std::nullopt;
		}
		return a_known;
	}
}
