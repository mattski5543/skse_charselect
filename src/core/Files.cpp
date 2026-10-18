// Writing files safely (see Files.h).

#include "core/Files.h"

#include "core/Encoding.h"

#include <format>
#include <fstream>

namespace charselect::files
{
	bool WriteAtomic(const std::filesystem::path& a_path, std::string_view a_text, std::string& a_error)
	{
		std::error_code ec;
		std::filesystem::create_directories(a_path.parent_path(), ec);
		auto tmp = a_path;
		tmp += ".tmp";
		const auto fail = [&](std::string a_why) {
			a_error = std::move(a_why);
			std::error_code ignored;
			std::filesystem::remove(tmp, ignored);  // no stray .tmp left behind
			return false;
		};
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out) {
				return fail(std::format("cannot open {}", text::PathUtf8(tmp)));
			}
			out.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
			out.flush();
			if (!out) {
				return fail(std::format("write to {} failed", text::PathUtf8(tmp)));
			}
		}
		std::filesystem::rename(tmp, a_path, ec);
		if (ec) {
			return fail(std::format("rename {} failed: {}", text::PathUtf8(tmp), ec.message()));
		}
		return true;
	}
}
