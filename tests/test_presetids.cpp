// Tests for rewriting RaceMenu's light-plugin form references.

#include "core/PresetIds.h"

#include <catch2/catch_test_macros.hpp>

using namespace charselect;

namespace
{
	bool KsLight(std::string_view a_plugin) { return a_plugin == "KSHairdosSMP.esp" || a_plugin == "Tiny.esl"; }
}

TEST_CASE("FixLightPluginIds strips the light slot from light-plugin references", "[presetids]")
{
	SECTION("a head part from a light plugin, as RaceMenu writes it")
	{
		std::string json = R"({"headParts":[{"formId":4263336187,"formIdentifier":"KSHairdosSMP.esp|1D58FB","type":6}]})";
		const auto  fixes = presetids::FixLightPluginIds(json, KsLight);
		REQUIRE(fixes.size() == 1);
		CHECK(fixes[0].plugin == "KSHairdosSMP.esp");
		CHECK(fixes[0].from == 0x1D58FB);
		CHECK(fixes[0].to == 0x8FB);
		CHECK(json == R"({"headParts":[{"formId":4263336187,"formIdentifier":"KSHairdosSMP.esp|0008FB","type":6}]})");
	}
	SECTION("everything else stays byte for byte, spacing included")
	{
		std::string json = "{\n  \"formIdentifier\" : \"KSHairdosSMP.esp|1D58FB\",\n  \"headTexture\" : \"Tiny.esl|FE0801\"\n}";
		CHECK(presetids::FixLightPluginIds(json, KsLight).size() == 2);
		CHECK(json == "{\n  \"formIdentifier\" : \"KSHairdosSMP.esp|0008FB\",\n  \"headTexture\" : \"Tiny.esl|000801\"\n}");
	}
}

TEST_CASE("FixLightPluginIds leaves everything else alone", "[presetids]")
{
	SECTION("regular plugins, already-clean IDs, and strings that aren't plugin references")
	{
		const std::string original = R"({"a":"Skyrim.esm|0F2058","b":"KSHairdosSMP.esp|0008FB","c":"Improved Eyes Skyrim.esp|0023FF",)"
		                             R"("d":"not a plugin|1D58FB","e":"KSHairdosSMP.esp","t":"Actors\\Character\\Head.dds"})";
		std::string json = original;
		CHECK(presetids::FixLightPluginIds(json, KsLight).empty());
		CHECK(json == original);
	}
	SECTION("a light plugin's name in another case is the callback's decision")
	{
		std::string json = R"({"x":"kshairdossmp.esp|1D58FB"})";
		CHECK(presetids::FixLightPluginIds(json, [](std::string_view) { return true; }).size() == 1);
		CHECK(json == R"({"x":"kshairdossmp.esp|0008FB"})");
	}
}
