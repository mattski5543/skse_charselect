#pragma once

// Small string helpers shared by the parsers.

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace charselect::text
{
	// Without leading and trailing spaces, tabs and line ends.
	inline std::string Trim(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = a_text.find_last_not_of(" \t\r\n");
		return std::string(a_text.substr(first, last - first + 1));
	}

	// ASCII lower case.
	inline std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}
}
