#pragma once

// Text from the engine (names, plugin filenames, save headers) is in the
// game's ANSI code page, not UTF-8. JSON and Scaleform need UTF-8.

#include <filesystem>
#include <string>
#include <string_view>

namespace charselect::text
{
	// The system ANSI code page, which a non-Unicode game like Skyrim uses.
	constexpr unsigned kGameCodePage = 0;  // CP_ACP

	[[nodiscard]] bool IsValidUtf8(std::string_view a_text);

	// a_text as UTF-8: unchanged if it already is valid UTF-8 (pure ASCII,
	// or strings a mod already wrote as UTF-8), else decoded from
	// a_codePage (bytes it can't map are replaced). Never throws.
	[[nodiscard]] std::string ToUtf8(std::string_view a_text, unsigned a_codePage = kGameCodePage);

	// The inverse, for handing text from our UTF-8 files back to the engine
	// (plugin names to look forms up by). Text that isn't valid UTF-8 is
	// returned unchanged. Characters the code page lacks become '?'.
	[[nodiscard]] std::string FromUtf8(std::string_view a_text, unsigned a_codePage = kGameCodePage);

	// A path as UTF-8, for logs and display. Never throws, unlike
	// path::string(), which throws for characters the ANSI code page lacks
	// (a Cyrillic file name on a Western system).
	[[nodiscard]] std::string PathUtf8(const std::filesystem::path& a_path);
}
