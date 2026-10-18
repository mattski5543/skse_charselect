// Tests for converting game text to UTF-8 and back.

#include "core/Encoding.h"

#include <catch2/catch_test_macros.hpp>

using namespace charselect::text;

namespace
{
	constexpr unsigned kWestern = 1252;
	constexpr unsigned kCyrillic = 1251;
}

TEST_CASE("IsValidUtf8", "[encoding]")
{
	CHECK(IsValidUtf8(""));
	CHECK(IsValidUtf8("Sven"));
	CHECK(IsValidUtf8("Bj\xC3\xB6rn"));            // ö
	CHECK(IsValidUtf8("\xE2\x82\xAC"));            // €
	CHECK(IsValidUtf8("\xF0\x9F\x90\x89"));        // 4-byte (dragon emoji)
	CHECK_FALSE(IsValidUtf8("Bj\xF6rn"));          // ö in Windows-1252
	CHECK_FALSE(IsValidUtf8("\xC3"));              // cut short
	CHECK_FALSE(IsValidUtf8("\xC3\x28"));          // bad continuation
	CHECK_FALSE(IsValidUtf8("\xC0\xAF"));          // overlong
	CHECK_FALSE(IsValidUtf8("\xED\xA0\x80"));      // surrogate
	CHECK_FALSE(IsValidUtf8("\xF4\x90\x80\x80"));  // past U+10FFFF
	CHECK_FALSE(IsValidUtf8("\xFF"));
}

TEST_CASE("ToUtf8 converts the game's code page", "[encoding]")
{
	CHECK(ToUtf8("Bj\xF6rn", kWestern) == "Bj\xC3\xB6rn");
	CHECK(ToUtf8("Fellr\xFCstung", kWestern) == "Fellr\xC3\xBCstung");  // German hide armour
	CHECK(ToUtf8("\x80", kWestern) == "\xE2\x82\xAC");
	CHECK(ToUtf8("\xCF\xF0\xE8\xE2\xE5\xF2", kCyrillic) == "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82");  // Привет
}

TEST_CASE("ToUtf8 leaves ASCII and UTF-8 alone", "[encoding]")
{
	CHECK(ToUtf8("", kWestern).empty());
	CHECK(ToUtf8("Skyrim.esm", kWestern) == "Skyrim.esm");
	CHECK(ToUtf8("Bj\xC3\xB6rn", kWestern) == "Bj\xC3\xB6rn");  // already UTF-8: not converted twice
	CHECK(ToUtf8("Bj\xC3\xB6rn", kCyrillic) == "Bj\xC3\xB6rn");
}

TEST_CASE("ToUtf8 always returns valid UTF-8", "[encoding]")
{
	std::string all;
	for (int c = 1; c < 256; ++c) {
		all.push_back(static_cast<char>(c));
	}
	for (const auto codePage : { kWestern, kCyrillic, kGameCodePage }) {
		INFO("code page " << codePage);
		CHECK(IsValidUtf8(ToUtf8(all, codePage)));
	}
}

TEST_CASE("FromUtf8 hands text back in the game's code page", "[encoding]")
{
	CHECK(FromUtf8("Bj\xC3\xB6rn", kWestern) == "Bj\xF6rn");
	CHECK(FromUtf8("Skyrim.esm", kWestern) == "Skyrim.esm");
	CHECK(FromUtf8("\xD0\x9F", kCyrillic) == "\xCF");
	CHECK(FromUtf8("\xD0\x9F", kWestern) == "?");      // the code page has no such letter
	CHECK(FromUtf8("Bj\xF6rn", kWestern) == "Bj\xF6rn");  // not UTF-8: unchanged
}

TEST_CASE("Plugin names survive the trip through a UTF-8 file", "[encoding]")
{
	for (const auto* name : { "Skyrim.esm", "Unofficial Skyrim Special Edition Patch.esp", "R\xFCstungen.esp", "Caf\xE9 Mod.esl" }) {
		INFO(name);
		CHECK(FromUtf8(ToUtf8(name, kWestern), kWestern) == name);
	}
}

TEST_CASE("PathUtf8 never throws", "[encoding]")
{
	CHECK(PathUtf8(std::filesystem::path(L"C:\\Users\\\u0418\u0432\u0430\u043D\\Saves")) ==
		  "C:\\Users\\\xD0\x98\xD0\xB2\xD0\xB0\xD0\xBD\\Saves");
	CHECK(PathUtf8(std::filesystem::path(L"Save1.ess")) == "Save1.ess");
	CHECK(PathUtf8(std::filesystem::path()).empty());
	// A lone surrogate (possible in NTFS names) still gives a string.
	CHECK_NOTHROW(PathUtf8(std::filesystem::path(std::wstring{ L'a', static_cast<wchar_t>(0xD800), L'b' })));
}
