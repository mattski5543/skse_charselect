// Tests for the select screen helpers: dates, left and right, key repeat.

#include "core/UiRules.h"

#include <catch2/catch_test_macros.hpp>

using namespace charselect::ui;

TEST_CASE("FriendlyDate", "[ui]")
{
	CHECK(FriendlyDate("2026-10-13 20:48:15") == "13 Oct 2026");
	CHECK(FriendlyDate("2026-01-05 00:00:00") == "5 Jan 2026");
	CHECK(FriendlyDate("1999-12-31") == "31 Dec 1999");
	CHECK(FriendlyDate("unknown") == "unknown");    // FormatFileTime's "no time"
	CHECK(FriendlyDate("") == "");
	CHECK(FriendlyDate("2026-13-01 00:00:00") == "2026-13-01 00:00:00");
	CHECK(FriendlyDate("2026-00-01 00:00:00") == "2026-00-01 00:00:00");
}

TEST_CASE("Left and right on the select screen", "[ui]")
{
	// Slots in on-screen order (the order the characters stand in).
	const std::vector<std::size_t> order{ 2, 0, 1 };
	CHECK(StepSelection(order, 0, -1) == 2u);
	CHECK(StepSelection(order, 0, 1) == 1u);
	CHECK_FALSE(StepSelection(order, 2, -1));  // no wrap at the left end
	CHECK_FALSE(StepSelection(order, 1, 1));   // or the right
	CHECK_FALSE(StepSelection(order, 7, 1));   // not shown
	CHECK_FALSE(StepSelection(order, 0, 0));
	CHECK_FALSE(StepSelection({}, 0, 1));
	CHECK_FALSE(StepSelection({ 4 }, 4, 1));
}

TEST_CASE("Key repeat in the editor", "[ui]")
{
	KeyRepeat repeat;
	SECTION("waits, then repeats at a steady rate")
	{
		CHECK_FALSE(repeat.Due(1, 0.0f));
		CHECK_FALSE(repeat.Due(1, 0.2f));
		CHECK(repeat.Due(1, 0.35f));
		CHECK_FALSE(repeat.Due(1, 0.38f));
		CHECK(repeat.Due(1, 0.45f));
		CHECK_FALSE(repeat.Due(1, 0.46f));
		CHECK(repeat.Due(1, 0.60f));
	}
	SECTION("a new press starts over")
	{
		CHECK(repeat.Due(1, 0.5f));
		CHECK_FALSE(repeat.Due(1, 0.0f));
		CHECK_FALSE(repeat.Due(1, 0.3f));
		CHECK(repeat.Due(1, 0.36f));
	}
	SECTION("buttons repeat independently")
	{
		CHECK(repeat.Due(1, 0.5f));
		CHECK_FALSE(repeat.Due(2, 0.1f));
		CHECK(repeat.Due(2, 0.4f));
		CHECK_FALSE(repeat.Due(1, 0.52f));
	}
	SECTION("a slow frame gives one repeat, not a burst")
	{
		CHECK(repeat.Due(1, 0.35f));
		CHECK(repeat.Due(1, 2.0f));
		CHECK_FALSE(repeat.Due(1, 2.01f));
	}
}

TEST_CASE("Key repeat reset on a fresh press", "[ui]")
{
	KeyRepeat repeat;
	CHECK(repeat.Due(1, 0.5f));
	repeat.Reset(1);
	CHECK_FALSE(repeat.Due(1, 0.1f));
	CHECK(repeat.Due(1, 0.36f));
}

TEST_CASE("Other mods' HUD menus hidden in the lobby", "[ui]")
{
	SECTION("a mod's always-open widget menu")
	{
		CHECK(IsOtherModsHudMenu("equipWidget_STB", true));
		CHECK(IsOtherModsHudMenu("TrueHUD", true));
	}
	SECTION("only menus that are always open")
	{
		CHECK_FALSE(IsOtherModsHudMenu("equipWidget_STB", false));
	}
	SECTION("never the game's own menus, nor the lobby's")
	{
		CHECK_FALSE(IsOtherModsHudMenu("HUD Menu", true));
		CHECK_FALSE(IsOtherModsHudMenu("Cursor Menu", true));
		CHECK_FALSE(IsOtherModsHudMenu("Fader Menu", true));
		CHECK_FALSE(IsOtherModsHudMenu("Loading Menu", true));
		CHECK_FALSE(IsOtherModsHudMenu("CharSelectMenu", true));
		CHECK_FALSE(IsOtherModsHudMenu("", true));
	}
}
