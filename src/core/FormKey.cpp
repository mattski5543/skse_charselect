// Reading and writing "0x000C8FC6|Skyrim.esm" form references (see FormKey.h).

#include "core/FormKey.h"

#include "core/Text.h"

#include <charconv>
#include <format>

namespace charselect
{
	std::optional<FormKey> ParseFormKey(std::string_view a_text)
	{
		const auto bar = a_text.find('|');
		if (bar == std::string_view::npos) {
			return std::nullopt;
		}
		auto id = text::Trim(a_text.substr(0, bar));
		auto plugin = text::Trim(a_text.substr(bar + 1));
		if (id.size() > 2 && id[0] == '0' && (id[1] == 'x' || id[1] == 'X')) {
			id.erase(0, 2);
		}
		if (id.empty() || id.size() > 8 || plugin.empty()) {
			return std::nullopt;
		}
		FormKey key;
		const auto [ptr, ec] = std::from_chars(id.data(), id.data() + id.size(), key.localId, 16);
		if (ec != std::errc{} || ptr != id.data() + id.size()) {
			return std::nullopt;
		}
		key.plugin = std::move(plugin);
		return key;
	}

	std::string FormatFormKey(const FormKey& a_key)
	{
		return std::format("0x{:08X}|{}", a_key.localId, a_key.plugin);
	}

	std::optional<std::string_view> StripPlacePrefix(std::string_view a_text)
	{
		constexpr std::string_view prefix = "place:";
		if (a_text.size() < prefix.size() || text::Lower(a_text.substr(0, prefix.size())) != prefix) {
			return std::nullopt;
		}
		return a_text.substr(prefix.size());
	}
}
