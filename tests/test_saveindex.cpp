// Tests for reading save headers and listing characters.

#include "SaveIndex.h"
#include "core/Encoding.h"
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdlib>

using namespace charselect;
using Catch::Matchers::ContainsSubstring;

// ---- save names -------------------------------------------------------------

TEST_CASE("ParseSaveName reads the game's save names", "[saves]")
{
	SECTION("a manual save")
	{
		const auto parts = ParseSaveName("Save7_1A2B3C4D_1_5376656E_Tamriel_071559_20261013204815_66_1");
		REQUIRE(parts);
		CHECK(parts->saveType == "Save");
		CHECK(parts->slot == 7);
		CHECK(parts->characterId == 0x1A2B3C4D);
	}
	SECTION("autosaves and quicksaves")
	{
		const auto autosave = ParseSaveName("Autosave3_5E6F7A8B_0_496E67726964_WhiterunBanneredMare_000004_20261017135626_1_1");
		REQUIRE(autosave);
		CHECK(autosave->saveType == "Autosave");
		CHECK(autosave->slot == 3);
		CHECK(autosave->characterId == 0x5E6F7A8B);

		const auto quick = ParseSaveName("Quicksave0_9C0D1E2F_1_476F726D_RiverwoodSleepingGiantInn_000001_20261014174236_1_1");
		REQUIRE(quick);
		CHECK(quick->saveType == "Quicksave");
		CHECK(quick->slot == 0);
	}
	SECTION("type names in any case, IDs in either hex case")
	{
		CHECK(ParseSaveName("save12_1a2b3c4d_x"));
		CHECK(ParseSaveName("AUTOSAVE1_1A2B3C4D_x"));
		CHECK(ParseSaveName("save12_1a2b3c4d_x")->characterId == 0x1A2B3C4D);
	}
	SECTION("big save numbers")
	{
		CHECK(ParseSaveName("Save1234_1A2B3C4D_x")->slot == 1234);
	}
	SECTION("a type without a number keeps slot 0")
	{
		const auto parts = ParseSaveName("Save_1A2B3C4D_x");
		REQUIRE(parts);
		CHECK(parts->slot == 0);
	}
	SECTION("an ID of zero is still an ID")
	{
		CHECK(ParseSaveName("Save1_00000000_x")->characterId == 0);
	}
}

TEST_CASE("ParseSaveName rejects names without a character ID", "[saves]")
{
	CHECK_FALSE(ParseSaveName(""));
	CHECK_FALSE(ParseSaveName("MySave"));                 // console "save MySave"
	CHECK_FALSE(ParseSaveName("Save7"));
	CHECK_FALSE(ParseSaveName("Save7_1A2B3C4D"));         // nothing after the ID
	CHECK_FALSE(ParseSaveName("Save7_1A2B3C4_x"));        // 7 hex digits
	CHECK_FALSE(ParseSaveName("Save7_1A2B3C4DA_x"));      // 9 hex digits
	CHECK_FALSE(ParseSaveName("Save7_1A2G3C4D_x"));       // not hex
	CHECK_FALSE(ParseSaveName("Save7__x"));               // empty ID
	CHECK_FALSE(ParseSaveName("Backup7_1A2B3C4D_x"));     // not a game save type
	CHECK_FALSE(ParseSaveName("7_1A2B3C4D_x"));           // no type
	CHECK_FALSE(ParseSaveName("Save7_+1A2B3C4_x"));       // sign
	CHECK_FALSE(ParseSaveName("Save7_0x1A2B3C_x"));       // C prefix
}

// ---- headers ----------------------------------------------------------------

TEST_CASE("ReadSaveHeader reads every field of an SE save", "[saves]")
{
	test::TempDir   dir;
	test::EssHeader h;
	h.playerSex = 1;
	const auto path = dir / "Save7_1A2B3C4D_x.ess";
	test::WriteEss(path, h);

	std::string error;
	const auto  header = ReadSaveHeader(path, error);
	REQUIRE(header);
	CHECK(error.empty());
	CHECK(header->version == 12);
	CHECK(header->saveNumber == 7);
	CHECK(header->playerName == "Sven");
	CHECK(header->playerLevel == 66);
	CHECK(header->playerLocation == "Whiterun");
	CHECK(header->gameDate == "4E 201, Morndas, 8:00 AM");
	CHECK(header->raceEditorId == "NordRace");
	CHECK(header->playerSex == 1);
	CHECK(header->playerCurExp == 12.5f);
	CHECK(header->playerLvlUpExp == 300.0f);
	CHECK(header->fileTime == h.fileTime);
	CHECK(header->shotWidth == 320);
	CHECK(header->shotHeight == 192);
	CHECK(header->compressionType == 2);
}

TEST_CASE("ReadSaveHeader handles older and unusual saves", "[saves]")
{
	test::TempDir dir;
	std::string   error;

	SECTION("version 9 (LE-era) has no compression field")
	{
		test::EssHeader h;
		h.version = 9;
		h.compressionType = 2;  // not written for this version
		test::WriteEss(dir / "a.ess", h);
		const auto header = ReadSaveHeader(dir / "a.ess", error);
		REQUIRE(header);
		CHECK(header->compressionType == 0);
		CHECK(header->playerName == "Sven");
	}
	SECTION("empty strings")
	{
		test::EssHeader h;
		h.playerName.clear();
		h.playerLocation.clear();
		test::WriteEss(dir / "a.ess", h);
		const auto header = ReadSaveHeader(dir / "a.ess", error);
		REQUIRE(header);
		CHECK(header->playerName.empty());
		CHECK(header->playerLocation.empty());
		CHECK(header->raceEditorId == "NordRace");
	}
	SECTION("a header bigger than the fields we read (future versions)")
	{
		test::EssHeader h;
		const auto      real = static_cast<std::uint32_t>(test::EssHeaderBytes(h).size());
		test::WriteEss(dir / "a.ess", h, real + 16);
		const auto header = ReadSaveHeader(dir / "a.ess", error);
		REQUIRE(header);
		CHECK(header->playerName == "Sven");
	}
}

TEST_CASE("ReadSaveHeader converts the game's code page to UTF-8", "[saves]")
{
	test::TempDir   dir;
	test::EssHeader h;
	h.playerName = "Bj\xF6rn";       // ö in Windows-1252
	h.playerLocation = "\x80 \x9C";  // euro sign, oe ligature
	test::WriteEss(dir / "a.ess", h);

	std::string error;
	const auto  western = ReadSaveHeader(dir / "a.ess", error, 1252);
	REQUIRE(western);
	CHECK(western->playerName == "Bj\xC3\xB6rn");
	CHECK(western->playerLocation == "\xE2\x82\xAC \xC5\x93");

	// The same bytes from a Russian game (Windows-1251) are Cyrillic.
	const auto russian = ReadSaveHeader(dir / "a.ess", error, 1251);
	REQUIRE(russian);
	CHECK(russian->playerName == "Bj\xD1\x86rn");  // 0xF6 is "ц" in 1251
	CHECK(text::IsValidUtf8(russian->playerLocation));
}

TEST_CASE("ReadSaveHeader rejects broken files with a reason", "[saves]")
{
	test::TempDir dir;
	std::string   error;

	SECTION("missing file")
	{
		CHECK_FALSE(ReadSaveHeader(dir / "nope.ess", error));
		CHECK(error == "cannot open");
	}
	SECTION("empty file")
	{
		test::WriteText(dir / "a.ess", "");
		CHECK_FALSE(ReadSaveHeader(dir / "a.ess", error));
		CHECK_THAT(error, ContainsSubstring("bad magic"));
	}
	SECTION("not a Skyrim save")
	{
		test::WriteText(dir / "a.ess", "FO4_SAVEGAME and some more bytes to read");
		CHECK_FALSE(ReadSaveHeader(dir / "a.ess", error));
		CHECK_THAT(error, ContainsSubstring("bad magic"));
	}
	SECTION("cut off after the magic")
	{
		test::WriteText(dir / "a.ess", "TESV_SAVEGAME\x01");
		CHECK_FALSE(ReadSaveHeader(dir / "a.ess", error));
		CHECK_THAT(error, ContainsSubstring("truncated before header size"));
	}
	SECTION("a header size past the end of the file")
	{
		test::EssHeader h;
		test::WriteEss(dir / "a.ess", h, 100000, 0);
		CHECK_FALSE(ReadSaveHeader(dir / "a.ess", error));
		CHECK_THAT(error, ContainsSubstring("exceeds read buffer"));
	}
	SECTION("a header size too small for its fields")
	{
		test::EssHeader h;
		test::WriteEss(dir / "a.ess", h, 20);
		CHECK_FALSE(ReadSaveHeader(dir / "a.ess", error));
		CHECK_THAT(error, ContainsSubstring("truncated"));
	}
	SECTION("a string longer than the header")
	{
		test::ByteWriter w;
		w.PutRaw("TESV_SAVEGAME").Put(std::uint32_t{ 12 }).Put(std::uint32_t{ 12 }).Put(std::uint32_t{ 1 }).Put(std::uint16_t{ 500 }).PutRaw("ab");
		std::ofstream(dir / "a.ess", std::ios::binary).write(reinterpret_cast<const char*>(w.bytes.data()), static_cast<std::streamsize>(w.bytes.size()));
		CHECK_FALSE(ReadSaveHeader(dir / "a.ess", error));
		CHECK_THAT(error, ContainsSubstring("truncated"));
	}
}

TEST_CASE("ReadSaveHeader only reads the first KiB", "[saves]")
{
	// A header that doesn't fit in the 1 KiB read is refused rather than read
	// past the buffer. Real headers are about 100 bytes plus the names.
	test::TempDir   dir;
	test::EssHeader h;
	h.playerLocation = std::string(1000, 'x');
	test::WriteEss(dir / "a.ess", h);
	std::string error;
	CHECK_FALSE(ReadSaveHeader(dir / "a.ess", error));
	CHECK_THAT(error, ContainsSubstring("exceeds read buffer"));
}

// ---- scanning a folder ------------------------------------------------------

namespace
{
	void WriteSave(const test::TempDir& a_dir, const std::string& a_stem, std::uint64_t a_fileTime, const std::string& a_name = "Sven",
		std::uint32_t a_level = 1)
	{
		test::EssHeader h;
		h.fileTime = a_fileTime;
		h.playerName = a_name;
		h.playerLevel = a_level;
		test::WriteEss(a_dir / (a_stem + ".ess"), h);
	}
}

TEST_CASE("ScanSaveDirectory groups saves by character, newest first", "[saves]")
{
	test::TempDir dir;
	WriteSave(dir, "Save1_AAAAAAAA_x", test::FileTime(2026, 10, 1), "Sven", 10);
	WriteSave(dir, "Save3_AAAAAAAA_x", test::FileTime(2026, 10, 3), "Sven", 12);
	WriteSave(dir, "Autosave1_AAAAAAAA_x", test::FileTime(2026, 10, 2), "Sven", 11);
	WriteSave(dir, "Save2_BBBBBBBB_x", test::FileTime(2026, 10, 5), "Ingrid", 4);
	WriteSave(dir, "Save9_CCCCCCCC_x", test::FileTime(2026, 9, 1), "Gorm", 30);

	const auto result = ScanSaveDirectory(dir.Path());
	CHECK(result.filesSeen == 5);
	CHECK(result.failures.empty());
	REQUIRE(result.characters.size() == 3);

	// Most recently played character first.
	CHECK(result.characters[0].characterId == 0xBBBBBBBB);
	CHECK(result.characters[1].characterId == 0xAAAAAAAA);
	CHECK(result.characters[2].characterId == 0xCCCCCCCC);

	const auto& sven = result.characters[1];
	REQUIRE(sven.saves.size() == 3);
	CHECK(sven.saves[0].stem == "Save3_AAAAAAAA_x");
	CHECK(sven.saves[1].stem == "Autosave1_AAAAAAAA_x");
	CHECK(sven.saves[2].stem == "Save1_AAAAAAAA_x");
	CHECK(sven.Latest().header.playerLevel == 12);
	CHECK(sven.Latest().name.slot == 3);
}

TEST_CASE("ScanSaveDirectory orders by the header's time, not the file name", "[saves]")
{
	// Save numbers restart for a character who gets a new ID, and an autosave
	// can be newer than a higher-numbered manual save.
	test::TempDir dir;
	WriteSave(dir, "Save99_AAAAAAAA_x", test::FileTime(2026, 1, 1));
	WriteSave(dir, "Save1_AAAAAAAA_x", test::FileTime(2026, 6, 1));
	const auto result = ScanSaveDirectory(dir.Path());
	REQUIRE(result.characters.size() == 1);
	CHECK(result.characters[0].Latest().stem == "Save1_AAAAAAAA_x");
}

TEST_CASE("ScanSaveDirectory skips what isn't a game save", "[saves]")
{
	test::TempDir dir;
	WriteSave(dir, "Save1_AAAAAAAA_x", test::FileTime(2026, 10, 1));
	test::WriteText(dir / "Save1_AAAAAAAA_x.skse", "co-save");                // SKSE co-save
	test::WriteText(dir / "notes.txt", "hello");
	std::filesystem::create_directories(dir / "Save2_BBBBBBBB_x.ess");        // a folder named like a save
	WriteSave(dir, "MyConsoleSave", test::FileTime(2026, 10, 2));             // no character ID
	test::WriteText(dir / "Save3_CCCCCCCC_x.ess", "corrupt");                 // unreadable header

	const auto result = ScanSaveDirectory(dir.Path());
	CHECK(result.filesSeen == 3);  // the three .ess files
	REQUIRE(result.characters.size() == 1);
	CHECK(result.characters[0].characterId == 0xAAAAAAAA);
	REQUIRE(result.failures.size() == 2);
	for (const auto& f : result.failures) {
		CHECK_FALSE(f.reason.empty());
	}
}

TEST_CASE("ScanSaveDirectory takes .ess in any case", "[saves]")
{
	test::TempDir dir;
	test::EssHeader h;
	test::WriteEss(dir / "Save1_AAAAAAAA_x.ESS", h);
	const auto result = ScanSaveDirectory(dir.Path());
	CHECK(result.characters.size() == 1);
}

TEST_CASE("ScanSaveDirectory reports a missing folder", "[saves]")
{
	test::TempDir dir;
	const auto    result = ScanSaveDirectory(dir / "missing");
	CHECK(result.characters.empty());
	CHECK(result.filesSeen == 0);
	REQUIRE(result.failures.size() == 1);
	CHECK_THAT(result.failures[0].reason, ContainsSubstring("directory iteration failed"));
}

TEST_CASE("ScanSaveDirectory on an empty folder", "[saves]")
{
	test::TempDir dir;
	const auto    result = ScanSaveDirectory(dir.Path());
	CHECK(result.characters.empty());
	CHECK(result.failures.empty());
	CHECK(result.filesSeen == 0);
}

TEST_CASE("ScanSaveDirectory copes with names the ANSI code page can't hold", "[saves]")
{
	// path::string() throws for these on a Western system, which crashed the
	// game at startup (the save scan runs at DataLoaded).
	test::TempDir dir;
	test::EssHeader h;
	test::WriteEss(dir.Path() / L"\u041F\u0440\u0438\u0432\u0435\u0442.ess", h);           // a custom name in Cyrillic
	test::WriteEss(dir.Path() / L"Save1_AAAAAAAA_\u4E2D\u6587_x.ess", h);                     // game pattern, Chinese suffix
	test::WriteText(dir.Path() / L"\u65E5\u672C.txt", "not a save");
	test::WriteEss(dir.Path() / L"Save2_BBBBBBBB_x.\u0435ss", h);                              // Cyrillic "e" in the extension

	REQUIRE_NOTHROW(ScanSaveDirectory(dir.Path()));
	const auto result = ScanSaveDirectory(dir.Path());
	CHECK(result.filesSeen == 2);
	REQUIRE(result.characters.size() == 1);
	CHECK(result.characters[0].characterId == 0xAAAAAAAA);
	CHECK(result.characters[0].Latest().stem == "Save1_AAAAAAAA_\xE4\xB8\xAD\xE6\x96\x87_x");  // UTF-8
	REQUIRE(result.failures.size() == 1);
	CHECK(text::PathUtf8(result.failures[0].path.filename()) == "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82.ess");
}

// ---- formatting -------------------------------------------------------------

TEST_CASE("FormatCharacterId is 8 upper-case hex digits", "[saves]")
{
	CHECK(FormatCharacterId(0x1A2B3C4D) == "1A2B3C4D");
	CHECK(FormatCharacterId(0xabc) == "00000ABC");
	CHECK(FormatCharacterId(0) == "00000000");
	CHECK(FormatCharacterId(0xFFFFFFFF) == "FFFFFFFF");
}

TEST_CASE("File times", "[saves]")
{
	CHECK(FileTimeToUnixSeconds(116444736000000000ULL) == 0);  // 1970-01-01
	CHECK(FileTimeToUnixSeconds(test::FileTime(2026, 10, 13, 20, 48, 15)) == 1791924495);
	CHECK(FormatFileTime(test::FileTime(2026, 10, 13, 20, 48, 15)) == "2026-10-13 20:48:15");
	CHECK(FormatFileTime(test::FileTime(2000, 2, 29, 23, 59, 59)) == "2000-02-29 23:59:59");
	CHECK(FormatFileTime(0) == "unknown");
}

// ---- real saves (opt-in) ----------------------------------------------------

TEST_CASE("Real saves folder parses cleanly", "[saves][real]")
{
	// Set CHARSELECT_TEST_SAVES to a saves folder to run this against real
	// game saves; skipped otherwise.
	char*       value = nullptr;
	std::size_t length = 0;
	if (_dupenv_s(&value, &length, "CHARSELECT_TEST_SAVES") != 0 || !value) {
		SKIP("CHARSELECT_TEST_SAVES not set");
	}
	const std::filesystem::path folder = value;
	std::free(value);

	const auto result = ScanSaveDirectory(folder);
	INFO("folder: " << folder.string());
	CHECK(result.filesSeen > 0);
	CHECK_FALSE(result.characters.empty());
	for (const auto& f : result.failures) {
		// Only custom-named saves may fail; every game-named save must parse.
		INFO(f.path.filename().string() << ": " << f.reason);
		CHECK_FALSE(ParseSaveName(f.path.stem().string()));
	}
	for (const auto& c : result.characters) {
		for (const auto& s : c.saves) {
			CHECK(s.header.version >= 9);
			CHECK_FALSE(s.header.playerName.empty());
			CHECK(s.header.fileTime > 0);
		}
	}
}
