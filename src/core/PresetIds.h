#pragma once

// RaceMenu saves a form from a light (ESL-flagged) plugin as
// "Plugin.esp|1D58FB": the low 24 bits of its load-order form ID, which hold
// the plugin's light slot (1D5) as well as its own ID (8FB). Its reader on
// AE doesn't strip the slot again, so once other light plugins are added or
// removed the reference points at the wrong form and the part is dropped
// (a mod hair replaced by a race default). Rewriting such IDs to the
// plugin's own part ("Plugin.esp|0008FB") makes them resolve in any load
// order, the way references to regular plugins already do.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace charselect::presetids
{
	struct Fix
	{
		std::string   plugin;
		std::uint32_t from{ 0 };
		std::uint32_t to{ 0 };
	};

	// Rewrites, in a preset's JSON text, every "<plugin>.esp|esm|esl|<hex>"
	// string whose plugin a_isLight says is light and whose ID has bits above
	// the low 12. Only those hex digits change; everything else stays byte for
	// byte. Returns what changed.
	std::vector<Fix> FixLightPluginIds(std::string& a_json, const std::function<bool(std::string_view)>& a_isLight);
}
