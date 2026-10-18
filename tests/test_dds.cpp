// Tests for the DDS textures written for lobby faces.

#include "core/Dds.h"
#include "TestUtil.h"

#include <catch2/catch_test_macros.hpp>

#include <sstream>

using namespace charselect;
using test::ReadAt;

namespace
{
	// DDS header offsets (DDS_HEADER after the 4-byte magic).
	constexpr std::size_t kSize = 4, kFlags = 8, kHeight = 12, kWidth = 16, kPitch = 20, kMips = 28;
	constexpr std::size_t kPfSize = 76, kPfFlags = 80, kFourCC = 84, kBitCount = 88, kRMask = 92, kGMask = 96, kBMask = 100, kAMask = 104;
	constexpr std::size_t kCaps = 108;
	constexpr std::size_t kDx10Format = 128, kDx10Dimension = 132, kDx10ArraySize = 140;

	std::uint32_t U32(const std::vector<std::uint8_t>& a_bytes, std::size_t a_offset) { return ReadAt<std::uint32_t>(a_bytes, a_offset); }
}

TEST_CASE("BytesPerPixel knows the tint formats", "[dds]")
{
	CHECK(dds::BytesPerPixel(dds::kR8G8B8A8_UNORM) == 4);
	CHECK(dds::BytesPerPixel(dds::kR8G8B8A8_UNORM_SRGB) == 4);
	CHECK(dds::BytesPerPixel(dds::kB8G8R8A8_UNORM) == 4);
	CHECK(dds::BytesPerPixel(dds::kB8G8R8A8_UNORM_SRGB) == 4);
	CHECK(dds::BytesPerPixel(dds::kR10G10B10A2_UNORM) == 4);
	CHECK(dds::BytesPerPixel(dds::kR16G16B16A16_FLOAT) == 8);
	CHECK(dds::BytesPerPixel(98) == 0);  // BC7: compressed, not read back
	CHECK(dds::BytesPerPixel(0) == 0);
}

TEST_CASE("An RGBA tint is written with a classic header and without row padding", "[dds]")
{
	test::TempDir dir;
	const auto    path = dir / "t.dds";
	// 2x2 pixels in rows padded to 16 bytes, as the GPU maps them.
	const std::vector<std::uint8_t> data{
		1, 2, 3, 4, 5, 6, 7, 8, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
		9, 10, 11, 12, 13, 14, 15, 16, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE
	};
	REQUIRE(dds::Write(path, 2, 2, dds::kR8G8B8A8_UNORM, data.data(), 16));
	CHECK_FALSE(std::filesystem::exists(dir / "t.dds.tmp"));

	const auto bytes = test::ReadBytes(path);
	REQUIRE(bytes.size() == 128 + 2 * 2 * 4);
	CHECK(std::string(bytes.begin(), bytes.begin() + 4) == "DDS ");
	CHECK(U32(bytes, kSize) == 124);
	CHECK(U32(bytes, kFlags) == 0x100F);
	CHECK(U32(bytes, kHeight) == 2);
	CHECK(U32(bytes, kWidth) == 2);
	CHECK(U32(bytes, kPitch) == 8);
	CHECK(U32(bytes, kMips) == 0);
	CHECK(U32(bytes, kPfSize) == 32);
	CHECK(U32(bytes, kPfFlags) == 0x41);  // RGB | ALPHAPIXELS
	CHECK(U32(bytes, kBitCount) == 32);
	CHECK(U32(bytes, kRMask) == 0x000000FF);
	CHECK(U32(bytes, kGMask) == 0x0000FF00);
	CHECK(U32(bytes, kBMask) == 0x00FF0000);
	CHECK(U32(bytes, kAMask) == 0xFF000000);
	CHECK(U32(bytes, kCaps) == 0x1000);
	const std::vector<std::uint8_t> pixels(bytes.begin() + 128, bytes.end());
	CHECK(pixels == std::vector<std::uint8_t>{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 });
}

TEST_CASE("A BGRA tint swaps the red and blue masks", "[dds]")
{
	test::TempDir                   dir;
	const std::vector<std::uint8_t> data(4, 0x7F);
	REQUIRE(dds::Write(dir / "t.dds", 1, 1, dds::kB8G8R8A8_UNORM, data.data(), 4));
	const auto bytes = test::ReadBytes(dir / "t.dds");
	CHECK(U32(bytes, kRMask) == 0x00FF0000);
	CHECK(U32(bytes, kBMask) == 0x000000FF);
	CHECK(bytes.size() == 132);
}

TEST_CASE("Other formats get the DX10 header", "[dds]")
{
	test::TempDir dir;
	for (const auto format : { dds::kR16G16B16A16_FLOAT, dds::kR8G8B8A8_UNORM_SRGB, dds::kR10G10B10A2_UNORM, dds::kB8G8R8A8_UNORM_SRGB }) {
		INFO("format " << format);
		const auto                      bpp = dds::BytesPerPixel(format);
		const std::vector<std::uint8_t> data(3 * 2 * bpp, 0x11);
		REQUIRE(dds::Write(dir / "t.dds", 3, 2, format, data.data(), 3 * bpp));
		const auto bytes = test::ReadBytes(dir / "t.dds");
		REQUIRE(bytes.size() == 128 + 20 + 3 * 2 * bpp);
		CHECK(U32(bytes, kPfFlags) == 0x4);  // FOURCC
		CHECK(std::string(bytes.begin() + kFourCC, bytes.begin() + kFourCC + 4) == "DX10");
		CHECK(U32(bytes, kPitch) == 3 * bpp);
		CHECK(U32(bytes, kDx10Format) == format);
		CHECK(U32(bytes, kDx10Dimension) == 3);  // TEXTURE2D
		CHECK(U32(bytes, kDx10ArraySize) == 1);
	}
}

TEST_CASE("Write replaces an existing file", "[dds]")
{
	test::TempDir dir;
	const auto    path = dir / "t.dds";
	test::WriteText(path, std::string(10000, 'x'));
	const std::vector<std::uint8_t> data(4, 0);
	REQUIRE(dds::Write(path, 1, 1, dds::kR8G8B8A8_UNORM, data.data(), 4));
	CHECK(std::filesystem::file_size(path) == 132);
}

TEST_CASE("Write refuses what it can't write correctly", "[dds]")
{
	test::TempDir                   dir;
	const std::vector<std::uint8_t> data(64, 0);
	CHECK_FALSE(dds::Write(dir / "a.dds", 2, 2, 98, data.data(), 8));                     // compressed format
	CHECK_FALSE(dds::Write(dir / "b.dds", 2, 2, dds::kR8G8B8A8_UNORM, nullptr, 8));       // no pixels
	CHECK_FALSE(dds::Write(dir / "c.dds", 4, 2, dds::kR8G8B8A8_UNORM, data.data(), 8));   // rows shorter than 4 pixels
	for (const auto* name : { "a.dds", "b.dds", "c.dds" }) {
		CHECK_FALSE(std::filesystem::exists(dir / name));
	}
	CHECK(dds::Write(dir / "new" / "d.dds", 1, 1, dds::kR8G8B8A8_UNORM, data.data(), 4));  // creates its folder
}

TEST_CASE("The placeholder is a flat, opaque skin tone of a known size", "[dds]")
{
	test::TempDir dir;
	const auto    path = dir / "p.dds";
	REQUIRE(dds::WritePlaceholder(path, 0x00C6B0A8));  // no alpha given: forced opaque

	const auto bytes = test::ReadBytes(path);
	// Tint.cpp tells a real export from the placeholder by file size.
	REQUIRE(bytes.size() == dds::kPlaceholderBytes);
	CHECK(bytes.size() == 4224);
	CHECK(U32(bytes, kWidth) == 32);
	CHECK(U32(bytes, kHeight) == 32);
	CHECK(U32(bytes, kRMask) == 0x00FF0000);  // BGRA in memory
	for (std::size_t offset = 128; offset < bytes.size(); offset += 4) {
		REQUIRE(U32(bytes, offset) == 0xFFC6B0A8);
	}
}

TEST_CASE("Any real tint export is bigger than the placeholder", "[dds]")
{
	// The smallest tint seen is 512x512; a 32x32 RGBA export would be the
	// same size as the placeholder and be taken for one.
	test::TempDir                   dir;
	const std::vector<std::uint8_t> data(64 * 64 * 4, 0);
	REQUIRE(dds::Write(dir / "t.dds", 64, 64, dds::kR8G8B8A8_UNORM, data.data(), 64 * 4));
	CHECK(std::filesystem::file_size(dir / "t.dds") > dds::kPlaceholderBytes);
}

TEST_CASE("Skin tone from a RaceMenu preset", "[dds]")
{
	const auto tone = [](const std::string& a_json) {
		std::istringstream in(a_json);
		return dds::SkinToneFromPreset(in);
	};

	SECTION("the tint layer with index 0")
	{
		CHECK(tone(R"({ "tintInfo": [ { "index": 3, "color": 1 }, { "index": 0, "color": 4291345575 } ] })") == 4291345575u);
	}
	SECTION("or a layer whose texture is the skin tone")
	{
		CHECK(tone(R"({ "tintInfo": [ { "index": 5, "texture": "Actors\\Character\\Character Assets\\TintMasks\\SkinTone.dds", "color": 42 } ] })") == 42u);
	}
	SECTION("the first matching layer wins")
	{
		CHECK(tone(R"({ "tintInfo": [ { "texture": "SkinTone.dds", "color": 1 }, { "index": 0, "color": 2 } ] })") == 1u);
	}
	SECTION("defaults")
	{
		CHECK(tone("{}") == dds::kDefaultSkinTone);
		CHECK(tone(R"({ "tintInfo": [] })") == dds::kDefaultSkinTone);
		CHECK(tone(R"({ "tintInfo": [ { "index": 2, "color": 5 } ] })") == dds::kDefaultSkinTone);
		CHECK(tone(R"({ "tintInfo": [ { "index": 0 } ] })") == dds::kDefaultSkinTone);  // no colour
	}
	SECTION("a broken preset throws (Tint.cpp logs it and uses the default)")
	{
		CHECK_THROWS(tone("{ not json"));
		CHECK_THROWS(tone(""));
	}
}
