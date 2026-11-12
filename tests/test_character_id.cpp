// Keeping a character's ID through RaceMenu: core/CharacterId.h.

#include "core/CharacterId.h"

#include <catch2/catch_test_macros.hpp>

using namespace charselect::character;

namespace
{
	constexpr std::uint32_t kOld = 0x1A2B3C4D;
	constexpr std::uint32_t kNew = 0x5E6F7A8B;
}

TEST_CASE("RaceMenu during play keeps the character's ID", "[character]")
{
	SECTION("after a save: the game's ID is the character's")
	{
		CHECK(IdToKeep(true, true, kOld, kOld, kNew) == kOld);
	}
	SECTION("straight after a load: the game hasn't set its ID yet (0), and the save's is put in")
	{
		CHECK(IdToKeep(true, true, kOld, 0, kNew) == kOld);
	}
}

TEST_CASE("The ID is left as the game made it otherwise", "[character]")
{
	SECTION("the setting off: the game's own way, a new character")
	{
		CHECK_FALSE(IdToKeep(false, true, kOld, kOld, kNew));
	}
	SECTION("a new game's character creation: no character known yet")
	{
		CHECK_FALSE(IdToKeep(true, true, std::nullopt, kOld, kNew));
		CHECK_FALSE(IdToKeep(true, true, std::nullopt, 0, kNew));
	}
	SECTION("in the lobby's game")
	{
		CHECK_FALSE(IdToKeep(true, false, kOld, kOld, kNew));
	}
	SECTION("the ID didn't change")
	{
		CHECK_FALSE(IdToKeep(true, true, kOld, kOld, kOld));
		CHECK_FALSE(IdToKeep(true, true, kOld, 0, 0));
	}
	SECTION("the game already has the character's ID")
	{
		CHECK_FALSE(IdToKeep(true, true, kOld, 0, kOld));
	}
	SECTION("the ID before was another character's: something else changed it, so it isn't ours to replace")
	{
		CHECK_FALSE(IdToKeep(true, true, kOld, 0x11111111, kNew));
	}
	SECTION("a save name with no real ID")
	{
		CHECK_FALSE(IdToKeep(true, true, 0u, 0, kNew));
	}
}
