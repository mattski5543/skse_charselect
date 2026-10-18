// Writing uncompressed DDS textures for the lobby faces (see Dds.h).

#include "core/Dds.h"

#include "core/Files.h"

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace charselect::dds
{
	namespace
	{
		// Files are built in memory and written with files::WriteAtomic, so a
		// crash or a full disk never leaves a half-written texture.
		void WriteU32(std::string& a_out, std::uint32_t a_value)
		{
			a_out.append(reinterpret_cast<const char*>(&a_value), sizeof(a_value));
		}
	}

	std::uint32_t BytesPerPixel(std::uint32_t a_format)
	{
		switch (a_format) {
		case kR8G8B8A8_UNORM:
		case kR8G8B8A8_UNORM_SRGB:
		case kB8G8R8A8_UNORM:
		case kB8G8R8A8_UNORM_SRGB:
		case kR10G10B10A2_UNORM:
			return 4;
		case kR16G16B16A16_FLOAT:
			return 8;
		default:
			return 0;
		}
	}

	bool Write(const std::filesystem::path& a_path, std::uint32_t a_width, std::uint32_t a_height, std::uint32_t a_format,
		const std::uint8_t* a_data, std::uint32_t a_rowPitch)
	{
		const auto bpp = BytesPerPixel(a_format);
		if (bpp == 0 || !a_data || a_rowPitch < a_width * bpp) {
			return false;
		}
		const bool rgba = a_format == kR8G8B8A8_UNORM;
		const bool bgra = a_format == kB8G8R8A8_UNORM;
		std::string out;
		out.reserve(148 + static_cast<std::size_t>(a_width) * a_height * bpp);
		{
			out.append("DDS ", 4);
			WriteU32(out, 124);
			WriteU32(out, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000);  // CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
			WriteU32(out, a_height);
			WriteU32(out, a_width);
			WriteU32(out, a_width * bpp);
			WriteU32(out, 0);  // depth
			WriteU32(out, 0);  // mips
			for (int i = 0; i < 11; ++i) {
				WriteU32(out, 0);
			}
			WriteU32(out, 32);
			if (rgba || bgra) {
				WriteU32(out, 0x41);  // RGB | ALPHAPIXELS
				WriteU32(out, 0);
				WriteU32(out, 32);
				WriteU32(out, rgba ? 0x000000FF : 0x00FF0000);
				WriteU32(out, 0x0000FF00);
				WriteU32(out, rgba ? 0x00FF0000 : 0x000000FF);
				WriteU32(out, 0xFF000000);
			} else {
				WriteU32(out, 0x4);  // FOURCC
				out.append("DX10", 4);
				for (int i = 0; i < 5; ++i) {
					WriteU32(out, 0);
				}
			}
			WriteU32(out, 0x1000);  // caps: texture
			for (int i = 0; i < 4; ++i) {
				WriteU32(out, 0);
			}
			if (!rgba && !bgra) {
				WriteU32(out, a_format);
				WriteU32(out, 3);  // texture 2D
				WriteU32(out, 0);
				WriteU32(out, 1);  // array size
				WriteU32(out, 0);
			}
			for (std::uint32_t y = 0; y < a_height; ++y) {
				out.append(reinterpret_cast<const char*>(a_data + static_cast<std::size_t>(y) * a_rowPitch), static_cast<std::size_t>(a_width) * bpp);
			}
		}
		std::string error;
		return files::WriteAtomic(a_path, out, error);
	}

	bool WritePlaceholder(const std::filesystem::path& a_path, std::uint32_t a_argb)
	{
		std::string out;
		out.reserve(kPlaceholderBytes);
		// DDS header: uncompressed 32-bit BGRA, no mipmaps.
		out.append("DDS ", 4);
		WriteU32(out, 124);                             // dwSize
		WriteU32(out, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000);  // CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
		WriteU32(out, kPlaceholderSize);                // height
		WriteU32(out, kPlaceholderSize);                // width
		WriteU32(out, kPlaceholderSize * 4);            // pitch
		WriteU32(out, 0);                               // depth
		WriteU32(out, 0);                               // mip count
		for (int i = 0; i < 11; ++i) {
			WriteU32(out, 0);  // reserved
		}
		WriteU32(out, 32);          // pixel format size
		WriteU32(out, 0x41);        // RGB | ALPHAPIXELS
		WriteU32(out, 0);           // fourCC
		WriteU32(out, 32);          // bit count
		WriteU32(out, 0x00FF0000);  // R mask
		WriteU32(out, 0x0000FF00);  // G mask
		WriteU32(out, 0x000000FF);  // B mask
		WriteU32(out, 0xFF000000);  // A mask
		WriteU32(out, 0x1000);      // caps: texture
		for (int i = 0; i < 4; ++i) {
			WriteU32(out, 0);  // caps2-4, reserved2
		}
		const std::uint32_t pixel = 0xFF000000 | (a_argb & 0x00FFFFFF);  // stored BGRA little-endian = ARGB value
		for (std::uint32_t i = 0; i < kPlaceholderSize * kPlaceholderSize; ++i) {
			WriteU32(out, pixel);
		}
		std::string error;
		return files::WriteAtomic(a_path, out, error);
	}

	std::uint32_t SkinToneFromPreset(std::istream& a_preset, std::uint32_t a_default)
	{
		const auto j = nlohmann::json::parse(a_preset);
		for (const auto& t : j.value("tintInfo", nlohmann::json::array())) {
			const auto texture = t.value("texture", std::string{});
			if (t.value("index", -1) == 0 || texture.find("SkinTone") != std::string::npos) {
				return t.value("color", a_default);
			}
		}
		return a_default;
	}
}
