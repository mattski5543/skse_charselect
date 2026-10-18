#pragma once

// Settings from Data\SKSE\Plugins\CharSelect.ini. Missing keys keep defaults.
// The settings and the parser live in core/ConfigParse.h.

#include "core/ConfigParse.h"

namespace charselect::config
{
	const Settings& Get();
	void            Load();
}
