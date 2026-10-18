// Plugin entry point: sets up logging, reads the settings, and passes SKSE's
// messages (data loaded, game saved, game loaded) to the rest of the plugin.

#include "Config.h"
#include "Events.h"
#include "Frontend.h"
#include "SelectMenu.h"
#include "Lobby.h"
#include "Paths.h"
#include "SaveIndex.h"
#include "Sidecar.h"
#include "Util.h"
#include "core/Encoding.h"

namespace
{
	std::string_view MessageName(std::uint32_t a_type)
	{
		using M = SKSE::MessagingInterface;
		switch (a_type) {
		case M::kPostLoad:
			return "PostLoad"sv;
		case M::kPostPostLoad:
			return "PostPostLoad"sv;
		case M::kPreLoadGame:
			return "PreLoadGame"sv;
		case M::kPostLoadGame:
			return "PostLoadGame"sv;
		case M::kSaveGame:
			return "SaveGame"sv;
		case M::kDeleteGame:
			return "DeleteGame"sv;
		case M::kInputLoaded:
			return "InputLoaded"sv;
		case M::kNewGame:
			return "NewGame"sv;
		case M::kDataLoaded:
			return "DataLoaded"sv;
		default:
			return "Unknown"sv;
		}
	}

	// SKSE passes the save name as a (not necessarily null-terminated) char buffer
	// of dataLen bytes for PreLoadGame / SaveGame / DeleteGame.
	std::string_view SaveNameFrom(const SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg->data || a_msg->dataLen == 0) {
			return "<none>"sv;
		}
		return { static_cast<const char*>(a_msg->data), a_msg->dataLen };
	}

	// Phase 1: log every character and their saves, newest first.
	void LogSaveIndex()
	{
		// Diagnostics only, so off the main thread: on a cold hard drive with
		// thousands of saves the scan can take seconds. The save
		// folder comes from the game's INI settings, so it's read here first.
		const auto dir = charselect::paths::SaveDir();
		if (!dir) {
			return;
		}
		std::thread([dir = *dir]() {
			charselect::Guarded("Save index", [&]() {
				logger::info("Save index: scanning {}", charselect::text::PathUtf8(dir));
				const auto result = charselect::ScanSaveDirectory(dir);
				logger::info("Save index: {} .ess files, {} characters, {} skipped, {:.1f} ms",
					result.filesSeen, result.characters.size(), result.failures.size(), result.elapsedMs);
				for (const auto& c : result.characters) {
					const auto& h = c.Latest().header;
					const bool  hasSidecar = std::filesystem::exists(charselect::paths::SidecarPath(c.characterId).value_or(""));
					const bool  hasPreset = std::filesystem::exists(charselect::paths::PresetPath(c.characterId));
					logger::info("Character {} '{}' lvl {} {} {} - {} saves, last {} (sidecar={}, preset={})",
						charselect::FormatCharacterId(c.characterId), h.playerName, h.playerLevel, h.raceEditorId,
						h.playerSex ? "female" : "male", c.saves.size(), charselect::FormatFileTime(h.fileTime), hasSidecar, hasPreset);
				}
				for (const auto& f : result.failures) {
					logger::info("Save index: skipped {}: {}", charselect::text::PathUtf8(f.path.filename()), f.reason);
				}
			});
		}).detach();
	}

	void HandleSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		using M = SKSE::MessagingInterface;
		switch (a_msg->type) {
		case M::kPreLoadGame:
			logger::info("SKSE message: {} (save='{}')", MessageName(a_msg->type), SaveNameFrom(a_msg));
			charselect::sidecar::OnPreLoadGame(SaveNameFrom(a_msg));
			charselect::lobby::OnPreLoadGame();
			break;
		case M::kNewGame:
			logger::info("SKSE message: {}", MessageName(a_msg->type));
			charselect::lobby::OnNewGame();
			break;
		case M::kSaveGame:
			logger::info("SKSE message: {} (save='{}')", MessageName(a_msg->type), SaveNameFrom(a_msg));
			if (charselect::lobby::GetState() != charselect::lobby::State::kIdle) {
				// Saving is blocked in the lobby; something got past it.
				logger::warn("SKSE message: a game was saved while the lobby is {}", charselect::lobby::StateName(charselect::lobby::GetState()));
			}
			charselect::sidecar::OnSaveGame(SaveNameFrom(a_msg));
			break;
		case M::kDeleteGame:
			logger::info("SKSE message: {} (save='{}')", MessageName(a_msg->type), SaveNameFrom(a_msg));
			break;
		case M::kPostLoadGame:
			{
				// For PostLoadGame SKSE stores the success flag in the pointer value itself.
				const bool success = a_msg->data != nullptr;
				logger::info("SKSE message: {} (success={})", MessageName(a_msg->type), success);
				charselect::lobby::OnPostLoadGame(success);
				charselect::sidecar::OnPostLoadGame(success);  // after the lobby has gone idle
			}
			break;
		case M::kDataLoaded:
			logger::info("SKSE message: {} - game data is loaded", MessageName(a_msg->type));
			charselect::SelectMenu::Register();
			charselect::frontend::Init();
			charselect::events::Register();
			LogSaveIndex();
			break;
		default:
			logger::info("SKSE message: {} ({})", MessageName(a_msg->type), a_msg->type);
			break;
		}
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		charselect::Guarded("SKSE message", [&]() { HandleSKSEMessage(a_msg); });
	}

	void InitializeLog(std::string_view a_pluginName)
	{
		auto path = logger::log_directory();
		if (!path) {
			SKSE::stl::report_and_fail("CharSelect: failed to find the SKSE log directory"sv);
		}
		*path /= std::format("{}.log", a_pluginName);

		// Keep the previous session's log (CharSelect.1.log): what went wrong
		// is often only noticed after a restart.
		std::error_code ec;
		auto            previous = *path;
		previous.replace_extension(".1.log");
		if (std::filesystem::exists(*path, ec)) {
			std::filesystem::rename(*path, previous, ec);
		}

		// path::string() throws for a Documents path the ANSI code page can't
		// hold, and the sink throws if the file can't be opened: run without a
		// log rather than fail to load.
		std::shared_ptr<spdlog::sinks::sink> sink;
		try {
			sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		} catch (const std::exception&) {
			sink = std::make_shared<spdlog::sinks::null_sink_mt>();
		}
		auto log = std::make_shared<spdlog::logger>("global"s, std::move(sink));

		// Logs are our only debugger: log everything and flush every line so a
		// crash never eats the last messages.
		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%s:%#] %v"s);
	}

	std::string_view RuntimeName()
	{
		if (REL::Module::IsAE()) {
			return "AE"sv;
		}
		if (REL::Module::IsSE()) {
			return "SE"sv;
		}
		return "other"sv;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	const auto plugin = SKSE::PluginDeclaration::GetSingleton();
	const auto name = plugin->GetName();
	const auto version = plugin->GetVersion();

	InitializeLog(name);
	logger::info("hello");
	logger::info("{} v{} loading", name, version.string("."sv));

	// log = false: by default SKSE::Init sets up its own logger, which reopens
	// (truncates) our log file and replaces our level and pattern.
	SKSE::Init(a_skse, { .log = false });

	const auto skseVersion = a_skse->SKSEVersion();
	logger::info("Game runtime: {} ({})", a_skse->RuntimeVersion().string("."sv), RuntimeName());
	logger::info("SKSE version: {}.{}.{}",
		(skseVersion >> 24) & 0xFF, (skseVersion >> 16) & 0xFF, (skseVersion >> 4) & 0xFFF);

	charselect::config::Load();

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		logger::critical("Failed to register SKSE messaging listener");
		return false;
	}
	logger::info("Registered SKSE messaging listener");

	logger::info("{} loaded", name);
	return true;
}
