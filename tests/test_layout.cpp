// Tests for the layout INI files, including the layouts that ship with the mod.

#include "core/FormKey.h"
#include "core/LayoutFile.h"
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <sstream>

using namespace charselect;
using namespace charselect::layout;
using Catch::Matchers::WithinAbs;

namespace
{
	IniFile Parse(const std::string& a_text)
	{
		std::istringstream in(a_text);
		return ParseIni(in);
	}

	std::string Write(const IniFile& a_file)
	{
		std::ostringstream out;
		WriteIni(out, a_file);
		return out.str();
	}

	IniFile ShippedLayouts()
	{
		std::ifstream in(std::filesystem::path(CHARSELECT_SOURCE_DIR) / "dist-files" / "SKSE" / "Plugins" / "CharSelectLayouts.ini");
		REQUIRE(in);
		return ParseIni(in);
	}
}

TEST_CASE("ParseIni", "[layout]")
{
	const auto file = Parse(R"(
; comment
[WhiterunBanneredMare]
cameraX = -124.6
  slot3furniture =  0x000C8FC6|Skyrim.esm
slot3pose=sit_drink

[Other]
key = a = b
)");
	REQUIRE(file.size() == 2);
	const auto& mare = file.at("whiterunbanneredmare");
	CHECK(mare.at("camerax") == "-124.6");
	CHECK(mare.at("slot3furniture") == "0x000C8FC6|Skyrim.esm");
	CHECK(mare.at("slot3pose") == "sit_drink");
	CHECK(file.at("other").at("key") == "a = b");  // split at the first '='
}

TEST_CASE("ParseIni keeps values with ';' and ':' whole", "[layout]")
{
	// No inline comments: "place:" values and plugin names must survive.
	const auto file = Parse("[c]\nslot1furniture = place:0x000B8371|Skyrim.esm\nslot2select = event:IdleWave;x\n");
	CHECK(file.at("c").at("slot1furniture") == "place:0x000B8371|Skyrim.esm");
	CHECK(file.at("c").at("slot2select") == "event:IdleWave;x");
}

TEST_CASE("ParseIni: later keys win, keys before a section go in the unnamed one", "[layout]")
{
	const auto file = Parse("orphan = 1\n[c]\nx = 1\nX = 2\n");
	CHECK(file.at("").at("orphan") == "1");
	CHECK(file.at("c").at("x") == "2");
}

TEST_CASE("ParseIni handles Windows line endings and an empty stream", "[layout]")
{
	CHECK(Parse("").empty());
	CHECK(Parse("[c]\r\nx = 1\r\n").at("c").at("x") == "1");
}

TEST_CASE("WriteIni and ParseIni round-trip", "[layout]")
{
	IniFile file;
	file["whiterunbanneredmare"]["camerax"] = "-124.6";
	file["whiterunbanneredmare"]["slot1furniture"] = "place:0x000B8371|Skyrim.esm";
	file["other"]["slot2pose"] = "event:IdleWave";

	const auto text = Write(file);
	CHECK(text.starts_with("; CharSelect lobby layouts"));
	CHECK(text.find("[whiterunbanneredmare]\n") != std::string::npos);
	CHECK(text.find("camerax = -124.6\n") != std::string::npos);
	CHECK(Parse(text) == file);
}

TEST_CASE("Merge puts the user's keys over the defaults, key by key", "[layout]")
{
	auto defaults = Parse("[mare]\nslot1x = 1\nslot1y = 2\n[other]\nk = v\n");
	Merge(defaults, Parse("[mare]\nslot1x = 10\nslot2x = 20\n[mine]\nk = m\n"));
	CHECK(defaults.at("mare").at("slot1x") == "10");
	CHECK(defaults.at("mare").at("slot1y") == "2");
	CHECK(defaults.at("mare").at("slot2x") == "20");
	CHECK(defaults.at("other").at("k") == "v");
	CHECK(defaults.at("mine").at("k") == "m");
}

TEST_CASE("ReadCell reads a cell's layout", "[layout]")
{
	const auto file = Parse(R"(
[WhiterunBanneredMare]
camerax = -124.6
cameray = -590.0
cameraz = 68.3
camerayaw = 0.0035
camerapitch = 0.1317
stagex = -143
stagey = -351
stagez = 65.3
slot1x = -155
slot1y = -248
slot1z = 64
slot1heading = 3.1
slot1pose = draw_weapon
slot9x = 1
slot9y = 2
slot9z = 3
slot3pose = sit_drink
slot3select = stand
slot3furniture = 0x000C8FC6|Skyrim.esm
)");
	const auto layout = ReadCell(file, "whiterunbanneredmare");  // any case
	REQUIRE(layout.camera);
	CHECK(*layout.camera == Vec3{ -124.6f, -590.0f, 68.3f });
	CHECK_THAT(layout.cameraYaw, WithinAbs(0.0035, 1e-6));
	CHECK_THAT(layout.cameraPitch, WithinAbs(0.1317, 1e-6));
	REQUIRE(layout.stage);
	CHECK(*layout.stage == Vec3{ -143.0f, -351.0f, 65.3f });

	REQUIRE(layout.slots[0]);
	CHECK(layout.slots[0]->pos == Vec3{ -155.0f, -248.0f, 64.0f });
	CHECK_THAT(layout.slots[0]->heading, WithinAbs(3.1, 1e-6));
	CHECK_FALSE(layout.slots[1]);
	REQUIRE(layout.slots[8]);
	CHECK(layout.slots[8]->heading == 0.0f);  // heading defaults to 0

	CHECK(layout.poses[0].pose == "draw_weapon");
	CHECK(layout.poses[0].select.empty());
	CHECK(layout.poses[2].pose == "sit_drink");
	CHECK(layout.poses[2].select == "stand");
	CHECK(layout.poses[2].furniture == "0x000C8FC6|Skyrim.esm");
	CHECK(layout.poses[1].pose.empty());
}

TEST_CASE("ReadCell: a point needs all three coordinates, and numbers must parse", "[layout]")
{
	const auto file = Parse("[c]\ncamerax = 1\ncameray = 2\nstagex = 1\nstagey = 2\nstagez = abc\nslot1x = 1\nslot1y = 2\nslot1z = 3\nslot1heading = left\n");
	const auto layout = ReadCell(file, "c");
	CHECK_FALSE(layout.camera);   // no z
	CHECK_FALSE(layout.stage);    // z isn't a number
	REQUIRE(layout.slots[0]);     // a bad heading falls back to 0
	CHECK(layout.slots[0]->heading == 0.0f);
}

TEST_CASE("ReadCell reads the time of day and weather", "[layout]")
{
	const auto set = ReadCell(Parse("[c]\nhour = 17.5\nweather = 0x0010A244|Skyrim.esm\n"), "c");
	REQUIRE(set.hour);
	CHECK(*set.hour == 17.5f);
	CHECK(set.weather == "0x0010A244|Skyrim.esm");

	const auto unset = ReadCell(Parse("[c]\ncamerax = 1\n"), "c");
	CHECK_FALSE(unset.hour);
	CHECK(unset.weather.empty());

	CHECK_FALSE(ReadCell(Parse("[c]\nhour = noon\n"), "c").hour);  // not a number
}

TEST_CASE("ReadCell for a cell with no section is empty", "[layout]")
{
	const auto layout = ReadCell(Parse("[a]\ncamerax = 1\n"), "b");
	CHECK_FALSE(layout.camera);
	CHECK_FALSE(layout.stage);
	for (std::size_t i = 0; i < kMaxSlots; ++i) {
		CHECK_FALSE(layout.slots[i]);
		CHECK(layout.poses[i].pose.empty());
	}
}

TEST_CASE("ReadCell ignores slots past 9", "[layout]")
{
	const auto layout = ReadCell(Parse("[c]\nslot10x = 1\nslot10y = 1\nslot10z = 1\nslot10pose = sit\n"), "c");
	for (const auto& slot : layout.slots) {
		CHECK_FALSE(slot);
	}
	// Not read as slot 1 either ("slot10" starts with "slot1").
	CHECK(layout.poses[0].pose.empty());
}

TEST_CASE("The editor's writes read back as they were saved", "[layout]")
{
	IniFile file;
	SetCamera(file, "WhiterunBanneredMare", { -124.64f, -590.0f, 68.34f }, 0.00351f, -0.1f);
	SetSlot(file, "WhiterunBanneredMare", 4, { -5.3f, -294.6f, 65.3f }, -2.7925f);
	SetSlotSetting(file, "WhiterunBanneredMare", 2, "furniture", "0x000C8FC6|Skyrim.esm");
	SetSlotSetting(file, "WhiterunBanneredMare", 2, "pose", "sit_drink");

	SECTION("written with fixed precision under the lower-cased cell")
	{
		const auto& section = file.at("whiterunbanneredmare");
		CHECK(section.at("camerax") == "-124.6");
		CHECK(section.at("cameraz") == "68.3");
		CHECK(section.at("camerayaw") == "0.0035");
		CHECK(section.at("camerapitch") == "-0.1000");
		CHECK(section.at("slot5x") == "-5.3");
		CHECK(section.at("slot5heading") == "-2.7925");
		CHECK(section.at("slot3furniture") == "0x000C8FC6|Skyrim.esm");
	}
	SECTION("and read back through a file")
	{
		const auto layout = ReadCell(Parse(Write(file)), "WhiterunBanneredMare");
		REQUIRE(layout.camera);
		CHECK_THAT(layout.camera->x, WithinAbs(-124.6, 1e-4));
		REQUIRE(layout.slots[4]);
		CHECK_THAT(layout.slots[4]->pos.y, WithinAbs(-294.6, 1e-4));
		CHECK_THAT(layout.slots[4]->heading, WithinAbs(-2.7925, 1e-4));
		CHECK(layout.poses[2].pose == "sit_drink");
		CHECK(layout.poses[2].furniture == "0x000C8FC6|Skyrim.esm");
	}
}

TEST_CASE("ClearCell removes one cell only (RESET TO DEFAULT)", "[layout]")
{
	IniFile file;
	SetSlot(file, "A", 0, {}, 0.0f);
	SetSlot(file, "B", 0, {}, 0.0f);
	ClearCell(file, "a");
	CHECK_FALSE(file.contains("a"));
	CHECK(file.contains("b"));
	ClearCell(file, "missing");  // no-op
	CHECK(file.size() == 1);
}

TEST_CASE("The shipped layouts are well formed", "[layout][shipped]")
{
	const auto file = ShippedLayouts();
	REQUIRE(file.contains("whiterunbanneredmare"));
	const auto mare = ReadCell(file, "WhiterunBanneredMare");
	CHECK(mare.camera);
	CHECK(mare.stage);
	for (std::size_t i = 0; i < 5; ++i) {
		INFO("slot " << i + 1);
		CHECK(mare.slots[i]);  // the five default characters have spots
	}
	for (const auto& [cell, section] : file) {
		for (const auto& [key, value] : section) {
			if (key == "weather") {
				INFO(cell << " weather = " << value);
				CHECK(ParseFormKey(value));
			}
			if (key.ends_with("furniture") && value != "none") {
				INFO(cell << " " << key << " = " << value);
				CHECK(ParseFormKey(StripPlacePrefix(value).value_or(value)));
			}
		}
	}
}

TEST_CASE("ParseIni skips a UTF-8 byte order mark", "[layout]")
{
	const auto file = Parse(std::string("\xEF\xBB\xBF") + "[c]\nx = 1\n");
	REQUIRE(file.contains("c"));
	CHECK(file.at("c").at("x") == "1");
}
