#pragma once

// Precompiled header for the plugin: CommonLibSSE-NG, SKSE and spdlog, plus
// the logger alias every file uses.

#define WIN32_LEAN_AND_MEAN

#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>

namespace logger = SKSE::log;

using namespace std::literals;
