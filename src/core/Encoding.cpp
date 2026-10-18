// Converting between the game's code page and UTF-8 (see Encoding.h). Built
// without the plugin's precompiled header (see CMakeLists.txt) so it can
// include <windows.h> without clashing with CommonLib's own declarations.
#include "core/Encoding.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <climits>

namespace charselect::text
{
	namespace
	{
		std::string AsciiOnly(std::string_view a_text)
		{
			std::string out(a_text);
			for (auto& c : out) {
				if (static_cast<unsigned char>(c) >= 0x80) {
					c = '?';
				}
			}
			return out;
		}
	}

	bool IsValidUtf8(std::string_view a_text)
	{
		std::size_t i = 0;
		const auto  n = a_text.size();
		while (i < n) {
			const auto c = static_cast<unsigned char>(a_text[i]);
			std::size_t len = 0;
			char32_t    cp = 0;
			if (c < 0x80) {
				++i;
				continue;
			} else if ((c & 0xE0) == 0xC0) {
				len = 2;
				cp = c & 0x1F;
			} else if ((c & 0xF0) == 0xE0) {
				len = 3;
				cp = c & 0x0F;
			} else if ((c & 0xF8) == 0xF0) {
				len = 4;
				cp = c & 0x07;
			} else {
				return false;
			}
			if (i + len > n) {
				return false;
			}
			for (std::size_t k = 1; k < len; ++k) {
				const auto cc = static_cast<unsigned char>(a_text[i + k]);
				if ((cc & 0xC0) != 0x80) {
					return false;
				}
				cp = (cp << 6) | (cc & 0x3F);
			}
			// Overlong forms, surrogates and values past U+10FFFF are invalid.
			if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
				(cp >= 0xD800 && cp <= 0xDFFF)) {
				return false;
			}
			i += len;
		}
		return true;
	}

	std::string ToUtf8(std::string_view a_text, unsigned a_codePage)
	{
		if (a_text.empty() || IsValidUtf8(a_text)) {
			return std::string(a_text);
		}
		if (a_text.size() < INT_MAX) {
			const auto size = static_cast<int>(a_text.size());
			const int  wide = ::MultiByteToWideChar(a_codePage, 0, a_text.data(), size, nullptr, 0);
			if (wide > 0) {
				std::wstring w(static_cast<std::size_t>(wide), L'\0');
				::MultiByteToWideChar(a_codePage, 0, a_text.data(), size, w.data(), wide);
				const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), wide, nullptr, 0, nullptr, nullptr);
				if (bytes > 0) {
					std::string out(static_cast<std::size_t>(bytes), '\0');
					::WideCharToMultiByte(CP_UTF8, 0, w.data(), wide, out.data(), bytes, nullptr, nullptr);
					return out;
				}
			}
		}
		// The conversion failed: keep ASCII, replace everything else.
		return AsciiOnly(a_text);
	}

	std::string PathUtf8(const std::filesystem::path& a_path)
	{
		const auto& wide = a_path.native();
		if (wide.empty() || wide.size() >= INT_MAX) {
			return {};
		}
		const auto size = static_cast<int>(wide.size());
		const int  bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
		if (bytes <= 0) {
			return {};
		}
		std::string out(static_cast<std::size_t>(bytes), ' ');
		::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, out.data(), bytes, nullptr, nullptr);
		return out;
	}

	std::string FromUtf8(std::string_view a_text, unsigned a_codePage)
	{
		const bool ascii = std::ranges::all_of(a_text, [](char c) { return static_cast<unsigned char>(c) < 0x80; });
		if (ascii || !IsValidUtf8(a_text) || a_text.size() >= INT_MAX) {
			return std::string(a_text);
		}
		const auto size = static_cast<int>(a_text.size());
		const int  wide = ::MultiByteToWideChar(CP_UTF8, 0, a_text.data(), size, nullptr, 0);
		if (wide > 0) {
			std::wstring w(static_cast<std::size_t>(wide), L'\0');
			::MultiByteToWideChar(CP_UTF8, 0, a_text.data(), size, w.data(), wide);
			const int bytes = ::WideCharToMultiByte(a_codePage, 0, w.data(), wide, nullptr, 0, nullptr, nullptr);
			if (bytes > 0) {
				std::string out(static_cast<std::size_t>(bytes), '\0');
				::WideCharToMultiByte(a_codePage, 0, w.data(), wide, out.data(), bytes, nullptr, nullptr);
				return out;
			}
		}
		return AsciiOnly(a_text);
	}
}
