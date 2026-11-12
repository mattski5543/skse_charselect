#pragma once

// Reading, writing and moving CharSelect's own files safely.

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace charselect::files
{
	// Writes a_text to a_path through a temporary file renamed over it, so a
	// crash mid-write never leaves a half-written file. Creates the folder.
	// On failure returns false and says why in a_error.
	bool WriteAtomic(const std::filesystem::path& a_path, std::string_view a_text, std::string& a_error);

	// The contents of a file about to be changed and written back: empty if
	// there's no such file, nullopt if it's there but can't be read (locked by
	// another program, no access), where writing back would lose what's in it.
	[[nodiscard]] std::optional<std::string> ReadForUpdate(const std::filesystem::path& a_path);

	// Moves a_from to a_to in one step, replacing a_to; creates a_to's folder.
	// On failure a_to is unchanged, and a_error says why.
	bool MoveReplacing(const std::filesystem::path& a_from, const std::filesystem::path& a_to, std::string& a_error);

	struct MoveResult
	{
		std::size_t                                               moved{ 0 };
		std::vector<std::pair<std::filesystem::path, std::string>> failed;  // file, why
	};
	// Moves every file in a_dir whose name starts with a_prefix and ends with
	// a_extension (either case) into a_target, replacing files there. Others
	// are left alone. A missing a_dir moves nothing.
	MoveResult MoveMatching(const std::filesystem::path& a_dir, const std::filesystem::path& a_target, std::string_view a_prefix, std::string_view a_extension);
}
