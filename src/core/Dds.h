#pragma once

// Uncompressed DDS files for the lobby faces, and the skin tone from a
// RaceMenu preset, free of the game so they can be unit tested. Tint.cpp
// reads the face texture from the GPU and calls these.

#include <cstdint>
#include <filesystem>
#include <istream>

namespace charselect::dds
{
	// DXGI_FORMAT values used by face tint textures.
	enum Format : std::uint32_t
	{
		kR16G16B16A16_FLOAT = 10,
		kR10G10B10A2_UNORM = 24,
		kR8G8B8A8_UNORM = 28,
		kR8G8B8A8_UNORM_SRGB = 29,
		kB8G8R8A8_UNORM = 87,
		kB8G8R8A8_UNORM_SRGB = 91,
	};

	// Bytes per pixel for the uncompressed formats a tint uses; 0 otherwise.
	[[nodiscard]] std::uint32_t BytesPerPixel(std::uint32_t a_format);

	// One mip level, rows a_rowPitch bytes apart in a_data (the GPU pads
	// rows). A classic header for 8-bit RGBA/BGRA, the DX10 extension for
	// anything else. Written to a temporary file renamed over a_path. False
	// for a format BytesPerPixel doesn't know, or rows shorter than a pixel row.
	bool Write(const std::filesystem::path& a_path, std::uint32_t a_width, std::uint32_t a_height, std::uint32_t a_format,
		const std::uint8_t* a_data, std::uint32_t a_rowPitch);

	// The flat skin-tone stand-in written when no real tint exists yet, so
	// the face never renders pink: 32x32 BGRA of a_argb's colour, opaque.
	constexpr std::uint32_t      kPlaceholderSize = 32;
	constexpr std::uintmax_t     kPlaceholderBytes = 128 + kPlaceholderSize * kPlaceholderSize * 4;
	bool                         WritePlaceholder(const std::filesystem::path& a_path, std::uint32_t a_argb);

	// The skin tone (ARGB) in a RaceMenu preset (.jslot): the tint layer
	// with index 0 or a SkinTone texture. a_default if there's none; throws
	// on a malformed preset.
	constexpr std::uint32_t      kDefaultSkinTone = 0xFFC6B0A8;
	[[nodiscard]] std::uint32_t  SkinToneFromPreset(std::istream& a_preset, std::uint32_t a_default = kDefaultSkinTone);
}
