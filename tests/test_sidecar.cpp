// Tests for the sidecar file format and safe file writing.

#include "core/Encoding.h"
#include "core/Files.h"
#include "core/SidecarJson.h"
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <nlohmann/json.hpp>

using namespace charselect;
using namespace charselect::sidecar;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace
{
	Sidecar Ingrid()
	{
		Sidecar s;
		s.characterId = 0x5E6F7A8B;
		s.saveName = "Save2_5E6F7A8B_1_496E67726964_WhiterunBanneredMare_000004_20261017135626_1_1";
		s.capturedAt = 1792248026;
		s.name = "Ingrid";
		s.level = 4;
		s.race = { "Skyrim.esm", 0x13746, "Imperial", "" };
		s.female = true;
		s.weight = 50.0f;
		s.height = 1.03f;
		s.preset = "CharSelect_5E6F7A8B";
		s.worn = { { "Skyrim.esm", 0x3619E, "Leather Armor", "" }, { "Skyrim.esm", 0x13135, "Iron Sword", "left" },
			{ "Skyrim.esm", 0x13135, "Iron Sword", "right" } };
		s.spells = { { "Skyrim.esm", 0x12FCD, "Flames", "left" } };
		s.plugins = { "Skyrim.esm", "Update.esm", "Unofficial Skyrim Special Edition Patch.esp" };
		return s;
	}
}

TEST_CASE("A sidecar survives a write and a read", "[sidecar]")
{
	const auto original = Ingrid();
	const auto text = ToJson(original).dump(2);
	CHECK(FromJson(json::parse(text)) == original);
}

TEST_CASE("The sidecar's JSON layout", "[sidecar]")
{
	const auto j = ToJson(Ingrid());
	CHECK(j["version"] == Sidecar::kVersion);
	CHECK(j["characterId"] == "5E6F7A8B");  // same form as the save names
	CHECK(j["race"]["formId"] == "00013746");
	CHECK(j["race"]["plugin"] == "Skyrim.esm");
	CHECK_FALSE(j["race"].contains("hand"));       // only items in hand have one
	CHECK(j["worn"][1]["hand"] == "left");
	CHECK(j["spells"][0]["hand"] == "left");
	CHECK(j["plugins"].size() == 3);
}

TEST_CASE("Empty lists are written as arrays", "[sidecar]")
{
	Sidecar s;
	const auto j = ToJson(s);
	CHECK(j["worn"].is_array());
	CHECK(j["spells"].is_array());
	CHECK(j["plugins"].is_array());
	CHECK(FromJson(j) == s);
}

TEST_CASE("Missing fields keep their defaults (older sidecars)", "[sidecar]")
{
	SECTION("an empty object")
	{
		const auto s = FromJson(json::object());
		CHECK(s.version == 0);
		CHECK(s.characterId == 0);
		CHECK(s.height == 1.0f);
		CHECK(s.worn.empty());
		CHECK(s.spells.empty());
		CHECK(s.race.plugin.empty());
	}
	SECTION("a version 1 file from before spells and hands (0.5.4)")
	{
		const auto s = FromJson(json::parse(R"({
			"version": 1, "characterId": "1A2B3C4D", "name": "Sven", "level": 66,
			"race": { "plugin": "Skyrim.esm", "formId": "00013746", "name": "Nord" },
			"worn": [ { "plugin": "Skyrim.esm", "formId": "00013135", "name": "Iron Sword" } ]
		})"));
		CHECK(s.characterId == 0x1A2B3C4D);
		CHECK(s.level == 66);
		REQUIRE(s.worn.size() == 1);
		CHECK(s.worn[0].hand.empty());
		CHECK(s.spells.empty());
	}
}

TEST_CASE("Malformed sidecars throw (Read logs them and skips the file)", "[sidecar]")
{
	CHECK_THROWS(FromJson(json::parse(R"({ "characterId": "not hex" })")));
	CHECK_THROWS(FromJson(json::parse(R"({ "characterId": 12345 })")));  // must be a string
	CHECK_THROWS(FromJson(json::parse(R"({ "worn": [ { "formId": "zz" } ] })")));
	CHECK_THROWS(FromJson(json::parse(R"({ "name": 5 })")));
	CHECK_THROWS(json::parse("{ truncated"));
}

TEST_CASE("Form IDs are read as hex", "[sidecar]")
{
	const auto s = FromJson(json::parse(R"({ "race": { "formId": "FE001801", "plugin": "x.esl" } })"));
	CHECK(s.race.localId == 0xFE001801);
}

TEST_CASE("Names from the engine are written as UTF-8 (H1)", "[sidecar]")
{
	// The engine hands over names in its code page: "Bj\xF6rn" in Windows-1252.
	// Written as is, nlohmann's dump() throws, which crashed the game on save.
	auto raw = Ingrid();
	raw.name = "Bj\xF6rn";
	CHECK_THROWS(ToJson(raw).dump(2));

	// What Sidecar.cpp does: convert at capture, and dump tolerantly anyway.
	auto s = Ingrid();
	s.name = text::ToUtf8("Bj\xF6rn", 1252);
	s.worn[0].name = text::ToUtf8("Fellr\xFCstung", 1252);
	s.plugins.push_back(text::ToUtf8("R\xFCstungen.esp", 1252));
	const auto out = ToJson(s).dump(2, ' ', false, json::error_handler_t::replace);
	const auto back = FromJson(json::parse(out));
	CHECK(back.name == "Bj\xC3\xB6rn");
	CHECK(back.worn[0].name == "Fellr\xC3\xBCstung");
	CHECK(text::FromUtf8(back.plugins.back(), 1252) == "R\xFCstungen.esp");

	// And a byte that slips through unconverted doesn't throw.
	CHECK_NOTHROW(ToJson(raw).dump(2, ' ', false, json::error_handler_t::replace));
}

TEST_CASE("When to re-export a preset", "[sidecar]")
{
	using Clock = std::chrono::steady_clock;
	const auto t0 = Clock::time_point{} + 1h;

	SECTION("never exported this session")
	{
		CHECK(NeedsPresetExport(false, std::nullopt, t0, false));
	}
	SECTION("exported recently: skipped (quicksaves, autosaves)")
	{
		CHECK_FALSE(NeedsPresetExport(false, t0, t0 + 1min, false));
		CHECK_FALSE(NeedsPresetExport(false, t0, t0 + 4min + 59s, false));
	}
	SECTION("exported 5 minutes ago or more")
	{
		CHECK(NeedsPresetExport(false, t0, t0 + 5min, false));
		CHECK(NeedsPresetExport(false, t0, t0 + 1h, false));
	}
	SECTION("files missing, or RaceMenu closed: always")
	{
		CHECK(NeedsPresetExport(true, t0, t0 + 1s, false));
		CHECK(NeedsPresetExport(false, t0, t0 + 1s, true));
	}
}

TEST_CASE("WriteAtomic", "[sidecar][files]")
{
	test::TempDir dir;
	std::string   error;

	SECTION("creates the folder and the file")
	{
		const auto path = dir / "a" / "b" / "5E6F7A8B.json";
		REQUIRE(files::WriteAtomic(path, "{}", error));
		CHECK(test::ReadText(path) == "{}");
		CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp"));
	}
	SECTION("replaces an existing file (every save rewrites the sidecar)")
	{
		const auto path = dir / "x.json";
		REQUIRE(files::WriteAtomic(path, "first, and longer", error));
		REQUIRE(files::WriteAtomic(path, "second", error));
		CHECK(test::ReadText(path) == "second");
	}
	SECTION("writes bytes as given (no newline translation)")
	{
		const auto path = dir / "x.json";
		REQUIRE(files::WriteAtomic(path, "a\nb", error));
		CHECK(test::ReadText(path) == "a\nb");
	}
	SECTION("fails with a reason when the target is a folder")
	{
		const auto path = dir / "taken";
		std::filesystem::create_directories(path);
		CHECK_FALSE(files::WriteAtomic(path, "x", error));
		CHECK_THAT(error, Catch::Matchers::ContainsSubstring("rename"));
	}
}

TEST_CASE("WriteAtomic leaves no temporary file when it fails", "[sidecar][files]")
{
	test::TempDir dir;
	const auto    path = dir / "taken";
	std::filesystem::create_directories(path);
	std::string error;
	REQUIRE_FALSE(files::WriteAtomic(path, "x", error));
	CHECK_FALSE(std::filesystem::exists(dir / "taken.tmp"));
}
