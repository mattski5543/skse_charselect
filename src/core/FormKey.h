#pragma once

// A form named by its plugin and its ID inside that plugin, as the INI files
// write it: "0x000C8FC6|Skyrim.esm". Layout furniture, "place:" furniture and
// the stand-in outfit all use this format.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace charselect
{
	struct FormKey
	{
		std::uint32_t localId{ 0 };
		std::string   plugin;

		bool operator==(const FormKey&) const = default;
	};

	// "0x000C8FC6|Skyrim.esm" (the 0x is optional, 1-8 hex digits, spaces
	// around either part ignored). nullopt if it isn't one.
	[[nodiscard]] std::optional<FormKey> ParseFormKey(std::string_view a_text);

	// The inverse: "0x000C8FC6|Skyrim.esm".
	[[nodiscard]] std::string FormatFormKey(const FormKey& a_key);

	// "place:0x000B8371|Skyrim.esm" (prefix in any case) -> the part after
	// "place:"; nullopt if a_text has no such prefix.
	[[nodiscard]] std::optional<std::string_view> StripPlacePrefix(std::string_view a_text);
}
