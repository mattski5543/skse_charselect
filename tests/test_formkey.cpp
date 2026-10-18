// Tests for "0x000C8FC6|Skyrim.esm" form references.

#include "core/FormKey.h"

#include <catch2/catch_test_macros.hpp>

using namespace charselect;

TEST_CASE("ParseFormKey reads plugin references", "[formkey]")
{
	SECTION("the format the editor writes")
	{
		const auto key = ParseFormKey("0x000C8FC6|Skyrim.esm");
		REQUIRE(key);
		CHECK(key->localId == 0x000C8FC6);
		CHECK(key->plugin == "Skyrim.esm");
	}
	SECTION("short IDs, either case, upper-case prefix, no prefix")
	{
		CHECK(ParseFormKey("0x801|My Mod.esp") == FormKey{ 0x801, "My Mod.esp" });
		CHECK(ParseFormKey("0xc8fc6|Skyrim.esm") == FormKey{ 0xC8FC6, "Skyrim.esm" });
		CHECK(ParseFormKey("0XC8FC6|Skyrim.esm") == FormKey{ 0xC8FC6, "Skyrim.esm" });
		CHECK(ParseFormKey("000C8FC6|Skyrim.esm") == FormKey{ 0xC8FC6, "Skyrim.esm" });
	}
	SECTION("spaces around either part (hand-edited INIs, the stand-in outfit list)")
	{
		CHECK(ParseFormKey("  0x000C5D10 | Skyrim.esm  ") == FormKey{ 0xC5D10, "Skyrim.esm" });
	}
	SECTION("plugin names keep their spaces and case")
	{
		CHECK(ParseFormKey("0x1|Cloaks of Skyrim.esp")->plugin == "Cloaks of Skyrim.esp");
	}
	SECTION("the full ID range")
	{
		CHECK(ParseFormKey("0xFFFFFFFF|a.esp")->localId == 0xFFFFFFFF);
		CHECK(ParseFormKey("0x0|a.esp")->localId == 0);
	}
}

TEST_CASE("ParseFormKey rejects anything else", "[formkey]")
{
	CHECK_FALSE(ParseFormKey(""));
	CHECK_FALSE(ParseFormKey("none"));
	CHECK_FALSE(ParseFormKey("0x000C8FC6"));          // no plugin
	CHECK_FALSE(ParseFormKey("0x000C8FC6|"));         // empty plugin
	CHECK_FALSE(ParseFormKey("0x000C8FC6|   "));
	CHECK_FALSE(ParseFormKey("|Skyrim.esm"));         // empty ID
	CHECK_FALSE(ParseFormKey("0x|Skyrim.esm"));
	CHECK_FALSE(ParseFormKey("0x12zz|Skyrim.esm"));   // trailing junk (stoul read this as 0x12)
	CHECK_FALSE(ParseFormKey("Bench|Skyrim.esm"));    // not hex
	CHECK_FALSE(ParseFormKey("-1|Skyrim.esm"));       // stoul wrapped this to 0xFFFFFFFF
	CHECK_FALSE(ParseFormKey("0x123456789|a.esp"));   // 9 digits
	CHECK_FALSE(ParseFormKey("0x 12|a.esp"));         // space inside
	CHECK_FALSE(ParseFormKey("place:0x1|a.esp"));     // the prefix must be stripped first
}

TEST_CASE("ParseFormKey splits at the first bar", "[formkey]")
{
	CHECK(ParseFormKey("0x1|a|b.esp")->plugin == "a|b.esp");
}

TEST_CASE("FormatFormKey is the inverse of ParseFormKey", "[formkey]")
{
	CHECK(FormatFormKey({ 0xC8FC6, "Skyrim.esm" }) == "0x000C8FC6|Skyrim.esm");
	CHECK(FormatFormKey({ 0, "a.esp" }) == "0x00000000|a.esp");
	for (const FormKey key : { FormKey{ 0x801, "My Mod.esp" }, FormKey{ 0xFFFFFFFF, "x.esl" }, FormKey{ 0x12, "Update.esm" } }) {
		CHECK(ParseFormKey(FormatFormKey(key)) == key);
	}
}

TEST_CASE("StripPlacePrefix", "[formkey]")
{
	CHECK(StripPlacePrefix("place:0x000B8371|Skyrim.esm") == "0x000B8371|Skyrim.esm");
	CHECK(StripPlacePrefix("PLACE:0x1|a.esp") == "0x1|a.esp");
	CHECK(StripPlacePrefix("place:") == "");
	CHECK_FALSE(StripPlacePrefix("0x000B8371|Skyrim.esm"));
	CHECK_FALSE(StripPlacePrefix("plac"));
	CHECK_FALSE(StripPlacePrefix(""));
	CHECK_FALSE(StripPlacePrefix(" place:0x1|a.esp"));  // not trimmed
}
