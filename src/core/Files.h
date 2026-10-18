#pragma once

// Writing CharSelect's own files safely.

#include <filesystem>
#include <string>
#include <string_view>

namespace charselect::files
{
	// Writes a_text to a_path through a temporary file renamed over it, so a
	// crash mid-write never leaves a half-written file. Creates the folder.
	// On failure returns false and says why in a_error.
	bool WriteAtomic(const std::filesystem::path& a_path, std::string_view a_text, std::string& a_error);
}
