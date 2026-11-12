// Reading, writing and moving CharSelect's own files safely (see Files.h).

#include "core/Files.h"

#include "core/Encoding.h"
#include "core/Text.h"

#include <format>
#include <fstream>
#include <iterator>

namespace charselect::files
{
	bool WriteAtomic(const std::filesystem::path& a_path, std::string_view a_text, std::string& a_error)
	{
		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);
		auto tmp = a_path;
		tmp += ".tmp";
		// File names only: the caller logs the folder (without the Windows
		// user name).
		const auto name = text::PathUtf8(tmp.filename());
		const auto fail = [&](std::string a_why) {
			a_error = std::move(a_why);
			std::error_code ignored;
			std::filesystem::remove(tmp, ignored);  // no stray .tmp left behind
			return false;
		};
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) {
				return fail(std::format("cannot open {}", name));
			}
			out.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
			out.flush();
			if (!out) {
				return fail(std::format("write to {} failed", name));
			}
		}
		std::filesystem::rename(tmp, a_path, ec);
		if (ec) {
			return fail(std::format("rename {} failed: {}", name, ec.message()));
		}
		return true;
	}

	std::optional<std::string> ReadForUpdate(const std::filesystem::path& a_path)
	{
		std::error_code ec;
		const bool      exists = std::filesystem::exists(a_path, ec);
		if (!exists && !ec) {
			return std::string{};
		}
		std::ifstream in(a_path, std::ios::binary);
		if (!in) {
			return std::nullopt;
		}
		std::string text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
		if (in.bad()) {
			return std::nullopt;
		}
		return text;
	}

	bool MoveReplacing(const std::filesystem::path& a_from, const std::filesystem::path& a_to, std::string& a_error)
	{
		std::error_code ec;
		std::filesystem::create_directories(a_to.parent_path(), ec);
		std::filesystem::rename(a_from, a_to, ec);  // replaces a_to in one step
		if (ec) {
			a_error = ec.message();
			return false;
		}
		return true;
	}

	MoveResult MoveMatching(const std::filesystem::path& a_dir, const std::filesystem::path& a_target, std::string_view a_prefix, std::string_view a_extension)
	{
		const auto      prefix = text::Lower(a_prefix);
		const auto      extension = text::Lower(a_extension);
		MoveResult      result;
		std::error_code ec;
		for (auto it = std::filesystem::directory_iterator(a_dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
			const auto& from = it->path();
			const auto  name = text::Lower(text::PathUtf8(from.filename()));
			std::error_code typeError;
			if (!it->is_regular_file(typeError) || !name.starts_with(prefix) || !name.ends_with(extension)) {
				continue;
			}
			std::string error;
			if (MoveReplacing(from, a_target / from.filename(), error)) {
				++result.moved;
			} else {
				result.failed.emplace_back(from, std::move(error));
			}
		}
		return result;
	}
}
