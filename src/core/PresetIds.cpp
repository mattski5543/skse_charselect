// Making RaceMenu's light-plugin form references load-order independent
// (see PresetIds.h).

#include "core/PresetIds.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <regex>

namespace charselect::presetids
{
	namespace
	{
		bool IsPluginName(std::string_view a_name)
		{
			if (a_name.size() < 5) {
				return false;
			}
			std::string ext(a_name.substr(a_name.size() - 4));
			std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return ext == ".esp" || ext == ".esm" || ext == ".esl";
		}
	}

	std::vector<Fix> FixLightPluginIds(std::string& a_json, const std::function<bool(std::string_view)>& a_isLight)
	{
		// A whole JSON string value: "Plugin Name.esp|1D58FB".
		static const std::regex reference(R"re("([^"|\\]+)\|([0-9A-Fa-f]{1,8})")re");

		std::vector<Fix> fixes;
		std::string      out;
		out.reserve(a_json.size());
		auto last = a_json.cbegin();
		for (std::sregex_iterator it(a_json.cbegin(), a_json.cend(), reference), end; it != end; ++it) {
			const auto& match = *it;
			const auto  plugin = match[1].str();
			const auto  id = static_cast<std::uint32_t>(std::stoul(match[2].str(), nullptr, 16));
			if (!IsPluginName(plugin) || id <= 0xFFF || !a_isLight(plugin)) {
				continue;
			}
			const auto to = id & 0xFFF;
			out.append(last, match[2].first);
			out += std::format("{:06X}", to);
			last = match[2].second;
			fixes.push_back({ plugin, id, to });
		}
		if (!fixes.empty()) {
			out.append(last, a_json.cend());
			a_json = std::move(out);
		}
		return fixes;
	}
}
