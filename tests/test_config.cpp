// Tests for reading CharSelect.ini, including the copy that ships with the mod.

#include "core/ConfigParse.h"
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <sstream>

using namespace charselect::config;
using Catch::Matchers::ContainsSubstring;

namespace
{
	Settings ParseText(const std::string& a_text, ParseLog* a_log = nullptr)
	{
		Settings           s;
		ParseLog           log;
		std::istringstream in(a_text);
		Parse(in, s, log);
		if (a_log) {
			*a_log = log;
		}
		return s;
	}

	std::filesystem::path ShippedIni()
	{
		return std::filesystem::path(CHARSELECT_SOURCE_DIR) / "dist-files" / "SKSE" / "Plugins" / "CharSelect.ini";
	}
}

TEST_CASE("An empty INI keeps the defaults", "[config]")
{
	ParseLog   log;
	const auto s = ParseText("", &log);
	const Settings d;
	CHECK(s.replaceMainMenu == d.replaceMainMenu);
	CHECK(s.lobbyHotkey == 0x41);
	CHECK(s.lobbyCell == "WhiterunBanneredMare");
	CHECK(s.fallbackCell == "qasmoke");
	CHECK(s.maxCharacters == 5);
	CHECK(s.standInOutfit.size() == 3);
	CHECK(log.infos.empty());
	CHECK(log.warnings.empty());
}

TEST_CASE("The shipped CharSelect.ini parses cleanly and matches the built-in defaults", "[config][shipped]")
{
	std::ifstream in(ShippedIni());
	REQUIRE(in);
	Settings s;
	ParseLog log;
	Parse(in, s, log);
	CHECK(log.warnings.empty());
	CHECK(log.infos.empty());

	// Deleting a line from the INI must not change behaviour, so the file and
	// the code agree on every default.
	const Settings d;
	CHECK(s.replaceMainMenu == d.replaceMainMenu);
	CHECK(s.lobbyHotkey == d.lobbyHotkey);
	CHECK(s.lobbyCell == d.lobbyCell);
	CHECK(s.fallbackCell == d.fallbackCell);
	CHECK(s.maxCharacters == d.maxCharacters);
	CHECK(s.standInOutfit == d.standInOutfit);
}

TEST_CASE("Every setting is read", "[config]")
{
	ParseLog   log;
	const auto s = ParseText(R"(
[General]
bReplaceMainMenu = 0
iLobbyHotkey = 0x42
[Lobby]
sLobbyCell = RiverwoodSleepingGiantInn
sFallbackCell = WhiterunDragonsreach
iMaxCharacters = 7
sStandInOutfit = 0x00012345|My Mod.esp
)",
		&log);
	CHECK(log.warnings.empty());
	CHECK_FALSE(s.replaceMainMenu);
	CHECK(s.lobbyHotkey == 0x42);
	CHECK(s.lobbyCell == "RiverwoodSleepingGiantInn");
	CHECK(s.fallbackCell == "WhiterunDragonsreach");
	CHECK(s.maxCharacters == 7);
	CHECK(s.standInOutfit == std::vector<std::string>{ "0x00012345|My Mod.esp" });
}

TEST_CASE("Sections and keys ignore case and spacing", "[config]")
{
	const auto s = ParseText("  [ general ]  \n\t BREPLACEMAINMENU\t=  false  \r\n[LOBBY]\nslobbycell=Foo\n");
	CHECK_FALSE(s.replaceMainMenu);
	CHECK(s.lobbyCell == "Foo");
}

TEST_CASE("Values keep their own case", "[config]")
{
	CHECK(ParseText("[Lobby]\nsLobbyCell = MyCell\n").lobbyCell == "MyCell");
}

TEST_CASE("Booleans", "[config]")
{
	for (const auto* yes : { "1", "true", "TRUE", "yes", "On" }) {
		INFO(yes);
		CHECK(ParseText(std::string("[General]\nbReplaceMainMenu = 0\nbReplaceMainMenu = ") + yes).replaceMainMenu);
	}
	for (const auto* no : { "0", "false", "no", "off", "", "2", "enabled" }) {
		INFO(no);
		CHECK_FALSE(ParseText(std::string("[General]\nbReplaceMainMenu = ") + no).replaceMainMenu);
	}
}

TEST_CASE("The hotkey takes hex or decimal", "[config]")
{
	CHECK(ParseText("[General]\niLobbyHotkey = 0x3B").lobbyHotkey == 0x3B);
	CHECK(ParseText("[General]\niLobbyHotkey = 59").lobbyHotkey == 59);
}

TEST_CASE("Comments", "[config]")
{
	ParseLog   log;
	const auto s = ParseText("; whole line\n# hash line\n[Lobby] ; after a section\nsLobbyCell = Foo ; after a value\niMaxCharacters = 3 # hash\n", &log);
	CHECK(log.warnings.empty());
	CHECK(s.lobbyCell == "Foo");
	CHECK(s.maxCharacters == 3);
}

TEST_CASE("iMaxCharacters is kept between 1 and 9", "[config]")
{
	CHECK(ParseText("[Lobby]\niMaxCharacters = 0").maxCharacters == 1);
	CHECK(ParseText("[Lobby]\niMaxCharacters = 1").maxCharacters == 1);
	CHECK(ParseText("[Lobby]\niMaxCharacters = 9").maxCharacters == 9);
	CHECK(ParseText("[Lobby]\niMaxCharacters = 50").maxCharacters == 9);
}

TEST_CASE("Bad values warn and keep the previous value", "[config]")
{
	ParseLog   log;
	const auto s = ParseText("[Lobby]\niMaxCharacters = lots\n[General]\niLobbyHotkey = F7\n", &log);
	CHECK(s.maxCharacters == 5);
	CHECK(s.lobbyHotkey == 0x41);
	REQUIRE(log.warnings.size() == 2);
	CHECK_THAT(log.warnings[0], ContainsSubstring("bad value for [lobby] imaxcharacters = 'lots'"));
}

TEST_CASE("Unknown keys and keys outside their section warn", "[config]")
{
	ParseLog log;
	ParseText("bReplaceMainMenu = 0\n[General]\nsLobbyCell = X\nsTypo = 1\n", &log);
	REQUIRE(log.warnings.size() == 3);
	CHECK_THAT(log.warnings[0], ContainsSubstring("unknown key [] breplacemainmenu"));
	CHECK_THAT(log.warnings[1], ContainsSubstring("unknown key [general] slobbycell"));
	CHECK_THAT(log.warnings[2], ContainsSubstring("unknown key [general] stypo"));
}

TEST_CASE("The removed automatic-layout settings are ignored with a note", "[config]")
{
	ParseLog   log;
	const auto s = ParseText("[Lobby]\nfRingRadius = 230\nfArcDegrees = 110\nfCameraDistance = 430\nfCameraPitch = 0.12\n", &log);
	CHECK(log.warnings.empty());
	REQUIRE(log.infos.size() == 4);
	CHECK_THAT(log.infos[0], ContainsSubstring("no longer used"));
	CHECK(s.maxCharacters == 5);
}

TEST_CASE("The removed quest option is ignored with a note", "[config]")
{
	ParseLog log;
	ParseText("[General]\nbSuppressStartupQuests = 1\n", &log);
	CHECK(log.warnings.empty());
	REQUIRE(log.infos.size() == 1);
	CHECK_THAT(log.infos[0], ContainsSubstring("no longer supported"));
}

TEST_CASE("Lines that aren't settings are skipped", "[config]")
{
	ParseLog   log;
	const auto s = ParseText("[Lobby]\njust some words\n[unclosed\nsLobbyCell = Foo\n", &log);
	CHECK(log.warnings.empty());
	CHECK(s.lobbyCell == "Foo");
}

TEST_CASE("The stand-in outfit list", "[config]")
{
	SECTION("comma-separated, spaces trimmed, empty items dropped")
	{
		const auto s = ParseText("[Lobby]\nsStandInOutfit =  0x1|A.esp ,, 0x2|B.esm ,\n");
		CHECK(s.standInOutfit == std::vector<std::string>{ "0x1|A.esp", "0x2|B.esm" });
	}
	SECTION("plugin names with spaces")
	{
		const auto s = ParseText("[Lobby]\nsStandInOutfit = 0x00000801|Cloaks of Skyrim.esp\n");
		CHECK(s.standInOutfit == std::vector<std::string>{ "0x00000801|Cloaks of Skyrim.esp" });
	}
	SECTION("a later line replaces an earlier one")
	{
		const auto s = ParseText("[Lobby]\nsStandInOutfit = 0x1|A.esp\nsStandInOutfit = 0x2|B.esp\n");
		CHECK(s.standInOutfit == std::vector<std::string>{ "0x2|B.esp" });
	}
	SECTION("an empty value means no outfit")
	{
		CHECK(ParseText("[Lobby]\nsStandInOutfit =\n").standInOutfit.empty());
	}
}

TEST_CASE("Windows line endings", "[config]")
{
	const auto s = ParseText("[Lobby]\r\nsLobbyCell = Foo\r\n");
	CHECK(s.lobbyCell == "Foo");
}

TEST_CASE("A UTF-8 BOM before the first section", "[config]")
{
	// Review L13: an editor that saves a byte order mark hid the first line
	// from the parser, and a file starting with a section lost its keys.
	ParseLog   log;
	const auto s = ParseText("\xEF\xBB\xBF[General]\nbReplaceMainMenu = 0\n", &log);
	CHECK_FALSE(s.replaceMainMenu);
	CHECK(log.warnings.empty());
}
