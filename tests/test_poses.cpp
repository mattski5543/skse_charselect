// Tests for pose names, the pose table and the editor's pose lists.

#include "core/FormKey.h"
#include "core/LayoutFile.h"
#include "core/PoseTable.h"
#include "core/Text.h"
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <set>

using namespace charselect;
using namespace charselect::poses;

TEST_CASE("Built-in pose names", "[poses]")
{
	const auto stand = ParsePose("stand");
	REQUIRE(stand.pose);
	CHECK_FALSE(stand.unknown);
	CHECK(stand.pose->name == "stand");
	CHECK_FALSE(stand.pose->sit);

	const auto drink = ParsePose("sit_drink");
	REQUIRE(drink.pose);
	CHECK(drink.pose->sit);
	CHECK(drink.pose->event == "ChairDrinkingStart");

	const auto weapon = ParsePose("draw_weapon");
	REQUIRE(weapon.pose);
	CHECK(weapon.pose->weaponDrawn);

	const auto sleep = ParsePose("sleep");
	REQUIRE(sleep.pose);
	CHECK(sleep.pose->sit);
	CHECK(sleep.pose->sleep);

	const auto floor = ParsePose("sit_floor");
	REQUIRE(floor.pose);
	CHECK(floor.pose->marker == "0x000C4EFD|Skyrim.esm");
}

TEST_CASE("Pose names ignore case", "[poses]")
{
	REQUIRE(ParsePose("SIT_Drink").pose);
	CHECK(ParsePose("SIT_Drink").pose->name == "sit_drink");
}

TEST_CASE("No pose, quietly", "[poses]")
{
	for (const auto* text : { "", "none", "None", "NONE" }) {
		INFO(text);
		const auto parsed = ParsePose(text);
		CHECK_FALSE(parsed.pose);
		CHECK_FALSE(parsed.unknown);
	}
}

TEST_CASE("Unknown names are flagged", "[poses]")
{
	for (const auto* text : { "dance", "sit drink", " stand", "event", "sit+event" }) {
		INFO(text);
		const auto parsed = ParsePose(text);
		CHECK_FALSE(parsed.pose);
		CHECK(parsed.unknown);
	}
}

TEST_CASE("Animation events keep what the user wrote", "[poses]")
{
	SECTION("standing")
	{
		const auto parsed = ParsePose("event:IdleWave");
		REQUIRE(parsed.pose);
		CHECK(parsed.pose->name == "event:IdleWave");
		CHECK(parsed.pose->event == "IdleWave");
		CHECK_FALSE(parsed.pose->sit);
	}
	SECTION("seated")
	{
		const auto parsed = ParsePose("sit+event:ChairReadingStart");
		REQUIRE(parsed.pose);
		CHECK(parsed.pose->event == "ChairReadingStart");
		CHECK(parsed.pose->sit);
	}
	SECTION("prefix in any case, event name untouched")
	{
		const auto parsed = ParsePose("EVENT:idleLaugh");
		REQUIRE(parsed.pose);
		CHECK(parsed.pose->event == "idleLaugh");
	}
}

TEST_CASE("The pose table is consistent", "[poses]")
{
	for (const auto& [key, pose] : Table()) {
		INFO(key);
		CHECK(key == pose.name);                         // looked up by its own name
		CHECK(key == text::Lower(key));                  // lookups lower-case the input
		CHECK((!pose.sleep || pose.sit));                // sleeping happens in furniture
		CHECK((!pose.weaponDrawn || !pose.sit));
		if (!pose.marker.empty()) {
			CHECK(pose.sit);                             // a marker is furniture to sit in
			CHECK(ParseFormKey(pose.marker));            // and must be a valid reference
		}
	}
}

TEST_CASE("The editor's pick lists only offer real poses", "[poses]")
{
	const auto& rest = PickList(false);
	const auto& select = PickList(true);

	SECTION("every entry parses to itself")
	{
		for (const auto& name : rest) {
			INFO(name);
			const auto parsed = ParsePose(name);
			REQUIRE(parsed.pose);
			CHECK(parsed.pose->name == name);
		}
		for (const auto& name : select) {
			INFO(name);
			if (name == "none") {
				continue;
			}
			const auto parsed = ParsePose(name);
			REQUIRE(parsed.pose);
			CHECK(parsed.pose->name == name);
		}
	}
	SECTION("the select list is \"none\", then the rest list, then one-shots")
	{
		REQUIRE(select.size() > rest.size() + 1);
		CHECK(select.front() == "none");
		CHECK(std::equal(rest.begin(), rest.end(), select.begin() + 1));
		CHECK(std::ranges::find(rest, "none") == rest.end());
		CHECK(std::ranges::find(rest, "applaud") == rest.end());  // one-shots only when selected
	}
	SECTION("no duplicates")
	{
		CHECK(std::set<std::string>(rest.begin(), rest.end()).size() == rest.size());
		CHECK(std::set<std::string>(select.begin(), select.end()).size() == select.size());
	}
	SECTION("rest starts with stand (the default)")
	{
		CHECK(rest.front() == "stand");
	}
}

TEST_CASE("Every pose in the shipped layouts exists", "[poses][shipped]")
{
	std::ifstream in(std::filesystem::path(CHARSELECT_SOURCE_DIR) / "dist-files" / "SKSE" / "Plugins" / "CharSelectLayouts.ini");
	REQUIRE(in);
	const auto file = layout::ParseIni(in);
	std::size_t checked = 0;
	for (const auto& [cell, section] : file) {
		for (const auto& [key, value] : section) {
			if (key.ends_with("pose") || key.ends_with("select")) {
				INFO(cell << " " << key << " = " << value);
				CHECK_FALSE(ParsePose(value).unknown);
				++checked;
			}
		}
	}
	CHECK(checked > 0);
}
