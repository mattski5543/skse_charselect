// Swaps the vanilla main menu for the character select screen (see Frontend.h).

#include "Frontend.h"

#include "Config.h"
#include "Lobby.h"
#include "ResetGuard.h"
#include "SelectMenu.h"
#include "Util.h"
#include "core/Text.h"

namespace charselect::frontend
{
	namespace
	{
		Action g_pending{ Action::kNone };
		bool   g_atMainMenu{ false };
		bool   g_classicThisVisit{ false };  // stay on the vanilla menu until it closes
		bool   g_viaHotkey{ false };         // this visit's lobby was asked for with the hotkey
		bool   g_covered{ false };           // CoverMainMenu hid it for the lobby this visit
		std::string g_failureMessage;        // a lobby that failed in its own game: shown at the main menu

		std::optional<std::chrono::steady_clock::time_point> g_mainMenuOpenedAt;  // until the lobby shows

		// RaceMenu applies every character's look; without it the lobby would
		// show nobody, so the vanilla menu stays.
		bool g_raceMenu{ true };
		bool g_warnedRaceMenu{ false };

		void WarnNoRaceMenu()
		{
			if (!std::exchange(g_warnedRaceMenu, true)) {
				RE::DebugMessageBox("CharSelect needs RaceMenu, which isn't installed or didn't load. Showing the main menu instead.");
			}
		}

		bool WillReplace()
		{
			return lobby::rules::ShouldReplaceMainMenu({ .replaceMainMenu = config::Get().replaceMainMenu,
				.raceMenuLoaded = g_raceMenu,
				.classicThisVisit = g_classicThisVisit,
				.actionPending = g_pending != Action::kNone,
				.state = lobby::GetState(),
				.loadFailedRecently = lobby::LoadFailedRecently(),
				.menuAvailable = !SelectMenu::MovieFailed() });
		}

		RE::GPtr<RE::IMenu> MainMenu()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui ? ui->GetMenu(RE::MainMenu::MENU_NAME) : nullptr;
		}

		void SetVanillaVisible(bool a_visible)
		{
			auto menu = MainMenu();
			if (menu && menu->uiMovie) {
				menu->uiMovie->SetVisible(a_visible);
				logger::debug("Frontend: vanilla main menu {}", a_visible ? "shown" : "hidden");
			}
		}

		// Calls the main menu callback whose name contains any of a_keywords
		// (case-insensitive), as if its button had been pressed. UI thread.
		bool InvokeMainMenuCallback(std::initializer_list<std::string_view> a_keywords)
		{
			auto menu = MainMenu();
			if (!menu || !menu->fxDelegate || !menu->uiMovie) {
				logger::error("Frontend: main menu not available for callback");
				return false;
			}
			for (const auto keyword : a_keywords) {
				const auto key = text::Lower(keyword);
				for (auto& entry : menu->fxDelegate->callbacks) {
					const std::string name = entry.first.c_str();
					if (text::Lower(name).find(key) == std::string::npos || !entry.second.callback) {
						continue;
					}
					logger::debug("Frontend: invoking main menu callback '{}' (matched '{}')", name, keyword);
					RE::FxDelegateArgs args(RE::GFxValue(), entry.second.handler.get(), menu->uiMovie.get(), nullptr, 0);
					entry.second.callback(args);
					return true;
				}
			}
			logger::warn("Frontend: no main menu callback matches any keyword");
			return false;
		}

		void PerformPending(Action a_action)
		{
			logger::info("Frontend: performing pending action {}", ActionName(a_action));
			if (a_action != Action::kNewGame) {
				return;
			}
			// The menu's own button handler, so on the thread its button runs on.
			QueueUITask([]() {
				if (!InvokeMainMenuCallback({ "startnewgame" })) {
					logger::error("Frontend: no main menu callback to start a new game");
					RE::DebugMessageBox("CharSelect: Could not start a new game. Use the main menu instead.");
				}
			});
		}

		void OnLobbyFailure(std::string_view a_reason, bool a_nothingToShow);
	}

	void OnSelectMenuUnavailable()
	{
		logger::error("Frontend: the select menu's movie didn't load; using the vanilla main menu this session");
		OnLobbyFailure("Its menu file (Interface\\CharSelectMenu.swf) couldn't be loaded, so the main menu is shown instead. Reinstalling CharSelect should fix this.", false);
	}

	namespace
	{
		void OnLobbyFailure(std::string_view a_reason, bool a_nothingToShow)
		{
			SelectMenu::Hide();
			if (g_atMainMenu) {
				g_classicThisVisit = true;
				SetVanillaVisible(true);
				if (a_nothingToShow && !g_viaHotkey) {
					// A new player's first launch: just the vanilla menu, no message.
					logger::info("Frontend: no saved characters yet; showing the vanilla main menu");
					return;
				}
				RE::DebugMessageBox(std::format("CharSelect: {}", a_reason).c_str());
			} else {
				// Failed in the lobby's own game: back to the main menu, and
				// say why once it's there.
				logger::warn("Frontend: the lobby failed in its game; returning to the main menu");
				g_failureMessage = std::format("CharSelect: {}", a_reason);
				ReturnToMainMenu(Action::kClassicMenu);
			}
		}

		void OnLobbyReady()
		{
			if (g_mainMenuOpenedAt) {
				const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - *g_mainMenuOpenedAt).count();
				logger::debug("Frontend: lobby shown {} ms after the main menu opened", ms);
				g_mainMenuOpenedAt.reset();
			}
			SelectMenu::ShowSelect();
		}

		constexpr auto kSplashPoll = std::chrono::milliseconds(50);
		constexpr int  kSplashPollMax = 20;  // 1 s

		// Still at the main menu: a mod that loads a save at startup may have
		// started one meanwhile, and the lobby must never start in it.
		bool StillAtMainMenu()
		{
			auto* ui = RE::UI::GetSingleton();
			return g_atMainMenu && ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME) && !ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME);
		}

		// The coc stalls the game, so wait for the splash to be up first.
		void EnterWhenSplashUp(int a_attempt)
		{
			if (lobby::GetState() != lobby::State::kIdle || SelectMenu::MovieFailed() || !StillAtMainMenu()) {
				return;
			}
			if (!SelectMenu::IsOpen() && a_attempt < kSplashPollMax) {
				RunAfter(kSplashPoll, [a_attempt]() { EnterWhenSplashUp(a_attempt + 1); });
				return;
			}
			// One more beat so it has drawn a frame.
			RunAfter(kSplashPoll, []() {
				if (StillAtMainMenu()) {
					lobby::RequestEnter();
				} else {
					logger::info("Frontend: no longer at the main menu; not starting the lobby");
				}
			});
		}

		void StartLobby()
		{
			if (lobby::GetState() != lobby::State::kIdle) {
				return;
			}
			SetVanillaVisible(false);
			SelectMenu::ShowSplash("Loading");
			EnterWhenSplashUp(0);
		}
	}

	std::string_view ActionName(Action a_action)
	{
		switch (a_action) {
		case Action::kNone:
			return "None"sv;
		case Action::kClassicMenu:
			return "ClassicMenu"sv;
		case Action::kNewGame:
			return "NewGame"sv;
		}
		return "?"sv;
	}

	void Init()
	{
		lobby::SetFailureHandler(OnLobbyFailure);
		lobby::SetReadyHandler(OnLobbyReady);
		g_raceMenu = REX::W32::GetModuleHandleW(L"skee64.dll") != nullptr;
		if (!g_raceMenu) {
			logger::warn("Frontend: RaceMenu (skee64.dll) isn't loaded; the lobby is off");
		}
	}

	void CoverMainMenu()
	{
		g_mainMenuOpenedAt = std::chrono::steady_clock::now();
		g_covered = WillReplace();
		if (!g_covered) {
			return;
		}
		auto menu = MainMenu();
		logger::debug("Frontend: covering the vanilla main menu as it opens (movie {})", menu && menu->uiMovie ? "hidden" : "not loaded yet");
		SetVanillaVisible(false);
		SelectMenu::ShowSplash("Loading");
	}

	void OnMainMenuOpened()
	{
		g_atMainMenu = true;
		g_viaHotkey = false;
		resetguard::Restore("main menu", false);
		lobby::OnMainMenuOpened();
		sidecar::OnMainMenuOpened();
		if (std::exchange(g_covered, false) && WillReplace()) {
			// Already covered: nothing from the vanilla menu is needed.
			StartLobby();
			return;
		}
		// The menu's movie and delegate are set up after the open event, so act a moment later.
		RunAfter(std::chrono::milliseconds(300), []() {
			if (!g_atMainMenu) {
				return;
			}
			if (g_pending != Action::kNone) {
				const auto action = std::exchange(g_pending, Action::kNone);
				g_classicThisVisit = true;
				SelectMenu::Hide();  // still black from the fade, if it survived the reset
				SetVanillaVisible(true);
				if (const auto message = std::exchange(g_failureMessage, {}); !message.empty()) {
					RE::DebugMessageBox(message.c_str());
				}
				PerformPending(action);
				return;
			}
			if (!config::Get().replaceMainMenu || g_classicThisVisit) {
				logger::info("Frontend: showing the vanilla main menu (replace={}, classic={})", config::Get().replaceMainMenu, g_classicThisVisit);
				SelectMenu::Hide();
				return;
			}
			if (!g_raceMenu) {
				SelectMenu::Hide();
				WarnNoRaceMenu();
				return;
			}
			if (lobby::LoadFailedRecently()) {
				// Re-entering would PLAY into the same failure again: a loop.
				logger::warn("Frontend: back at the main menu after a failed load from the lobby; showing the vanilla menu");
				lobby::ClearLoadFailure();
				g_classicThisVisit = true;
				SelectMenu::Hide();
				SetVanillaVisible(true);
				const auto key = config::Get().lobbyHotkey == 0x41 ? std::string("F7") : std::string("the CharSelect hotkey (iLobbyHotkey)");
				RE::DebugMessageBox(
					std::format("CharSelect: That save couldn't be loaded, so the game returned to the main menu. Press {} for the character select.", key)
						.c_str());
				return;
			}
			StartLobby();
		});
	}

	void OnMainMenuClosed()
	{
		g_atMainMenu = false;
		g_classicThisVisit = false;
	}

	void EnterFromHotkey()
	{
		if (!g_atMainMenu) {
			return;
		}
		g_classicThisVisit = false;
		if (!g_raceMenu) {
			g_warnedRaceMenu = false;
			WarnNoRaceMenu();
			return;
		}
		if (SelectMenu::MovieFailed()) {
			OnSelectMenuUnavailable();
			return;
		}
		g_viaHotkey = true;
		StartLobby();
	}

	void ReturnToMainMenu(Action a_action)
	{
		logger::info("Frontend: returning to the main menu, then {}", ActionName(a_action));
		g_pending = a_action;
		lobby::BeginLeaving(std::format("back to the main menu, then {}", ActionName(a_action)));
		// The select menu stays up (black after its fade) through the reset;
		// OnMainMenuOpened hides it once the vanilla menu is there. Saving
		// stays off until then (lobby::OnMainMenuOpened).
		lobby::ClearPresets([]() {
			if (g_atMainMenu) {
				logger::info("Frontend: already at the main menu; no reset needed");
				lobby::OnMainMenuOpened();
				return;
			}
			auto* main = RE::Main::GetSingleton();
			if (!main) {
				logger::error("Frontend: no Main singleton");
				return;
			}
			resetguard::Capture("main menu");
			// What the game's own Quit to Main Menu sets: the main loop ends
			// the running game and opens the main menu.
			main->GetRuntimeData().resetGame = true;
			RunAfter(std::chrono::seconds(8), []() {
				if (!g_atMainMenu) {
					logger::warn("Frontend: still not at the main menu 8 s after asking for it");
				}
			});
		});
	}

	namespace
	{
		constexpr auto kJournalPoll = std::chrono::milliseconds(100);
		constexpr int  kJournalPollMax = 30;                              // 3 s
		constexpr auto kJournalTabSettle = std::chrono::milliseconds(300);  // let the page switch finish

		// The Journal's own ActionScript (vanilla and SkyUI's quest_journal.swf,
		// read from its bytecode):
		//   Menu_mc.RestoreSavedSettings(tab, tabsDisabled) picks the tab (2 = System)
		//   SystemPage.__set__currentState(state) swaps in that state's panel
		//   SystemPage.StartState(state) sets that state up; CHARACTER_LOAD_STATE
		//     is what its T key / gamepad Y does on the load page: the list of
		//     all characters (the load page itself only shows the current
		//     character's saves, and the lobby's character has none).
		// All on the UI thread.
		constexpr const char* kJournalRoot = "_root.QuestJournalFader.Menu_mc";
		constexpr const char* kSystemPage = "_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc";

		RE::GFxMovieView* JournalMovie()
		{
			auto* ui = RE::UI::GetSingleton();
			auto  menu = ui ? ui->GetMenu(RE::JournalMenu::MENU_NAME) : nullptr;
			return menu ? menu->uiMovie.get() : nullptr;
		}

		// The steps the load page takes itself: show the save/load panel and
		// its buttons, then what its T key does there, fill the panel with
		// every character. UI thread, inside the Journal's movie.
		bool ShowCharacterList(RE::GFxMovie* a_movie, RE::GFxValue& a_page)
		{
			RE::GFxValue holder, saveLoad, characters;
			if (!a_page.GetMember("SaveLoadListHolder", &holder) || !holder.IsObject() ||
				!a_movie->GetVariable(&saveLoad, "_global.SystemPage.SAVE_LOAD_STATE") || !saveLoad.IsNumber() ||
				!a_movie->GetVariable(&characters, "_global.SystemPage.CHARACTER_LOAD_STATE") || !characters.IsNumber()) {
				logger::warn("Frontend: Journal save/load panel or its states not found");
				return false;
			}
			holder.SetMember("isSaving", RE::GFxValue(false));
			const bool panel = a_page.Invoke("__set__currentState", nullptr, &saveLoad, 1);
			const bool buttons = a_page.Invoke("StartState", nullptr, &saveLoad, 1);
			const bool list = a_page.Invoke("StartState", nullptr, &characters, 1);
			logger::debug("Frontend: Journal character list: panel {}, buttons {}, characters {}", panel, buttons, list);
			return panel && buttons && list;
		}

		bool IsLoadEntry(RE::GFxValue& a_entry)
		{
			RE::GFxValue text;
			if (!a_entry.IsObject() || !a_entry.GetMember("text", &text) || !text.IsString()) {
				return false;
			}
			std::string label = text.GetString();
			std::ranges::transform(label, label.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
			return label.find("LOAD") != std::string::npos;
		}

		// Where the page's own entry handler is kept while ours is installed:
		// on the page itself, so no C++ code holds a value from the Journal's
		// movie past its lifetime.
		constexpr const char* kOriginalPress = "charSelectOriginalCategoryPress";

		// Stands in for SystemPage.onCategoryButtonPress while the Journal is
		// open over the lobby: LOAD opens the list of all characters (the
		// page's own LOAD would list the lobby character's saves, which don't
		// exist); every other entry goes to the original handler.
		class CategoryPressHandler : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				Guarded("Journal LOAD entry", [&]() { Handle(a_params); });
			}

		private:
			static void Handle(Params& a_params)
			{
				RE::GFxValue entry;
				if (a_params.argCount > 0 && a_params.args[0].IsObject() && a_params.args[0].GetMember("entry", &entry) && IsLoadEntry(entry) &&
					lobby::GetState() == lobby::State::kActive) {
					ShowCharacterList(a_params.movie, *a_params.thisPtr);
					return;
				}
				RE::GFxValue original;
				if (a_params.thisPtr->GetMember(kOriginalPress, &original) && original.IsObject()) {
					std::array<RE::GFxValue, 2> args{ *a_params.thisPtr, a_params.argCount > 0 ? a_params.args[0] : RE::GFxValue() };
					original.Invoke("call", a_params.retVal, args.data(), args.size());
				}
			}
		};

		// LOAD greyed out (no saves for the lobby's character) blocks the
		// page's navigation; enable it and take over what it does.
		void TakeOverLoadEntry(RE::GFxMovie* a_movie, RE::GFxValue& a_page)
		{
			RE::GFxValue list, entries;
			if (a_page.GetMember("CategoryList", &list) && list.GetMember("entryList", &entries) && entries.IsArray()) {
				for (std::uint32_t i = 0; i < entries.GetArraySize(); ++i) {
					RE::GFxValue entry;
					if (entries.GetElement(i, &entry) && IsLoadEntry(entry)) {
						entry.SetMember("disabled", RE::GFxValue(false));
						list.Invoke("UpdateList");
						break;
					}
				}
			}
			RE::GFxValue installed;
			if (a_page.GetMember(kOriginalPress, &installed) && installed.IsObject()) {
				return;  // already ours (the same Journal opened again)
			}
			RE::GFxValue original;
			if (!a_page.GetMember("onCategoryButtonPress", &original) || !original.IsObject()) {
				logger::warn("Frontend: Journal entry handler not found; LOAD stays as it is");
				return;
			}
			auto         handler = RE::make_gptr<CategoryPressHandler>();
			RE::GFxValue replacement;
			a_movie->CreateFunction(&replacement, handler.get());
			a_page.SetMember(kOriginalPress, original);
			a_page.SetMember("onCategoryButtonPress", replacement);
			logger::info("Frontend: Journal LOAD enabled and routed to the character list");
		}

		void OpenCharacterList()
		{
			QueueUITask([]() {
				auto*        movie = JournalMovie();
				RE::GFxValue page;
				if (!movie || !movie->GetVariable(&page, kSystemPage) || !page.IsObject()) {
					logger::warn("Frontend: Journal System page not found; it stays on the System tab");
					return;
				}
				TakeOverLoadEntry(movie, page);
				ShowCharacterList(movie, page);
			});
		}

		void DriveJournal(int a_attempt)
		{
			QueueUITask([a_attempt]() {
				auto*        movie = JournalMovie();
				RE::GFxValue menu;
				if (!movie || !movie->GetVariable(&menu, kJournalRoot) || !menu.IsObject()) {
					if (a_attempt < kJournalPollMax) {
						RunAfter(kJournalPoll, [a_attempt]() { DriveJournal(a_attempt + 1); });
					} else {
						logger::warn("Frontend: Journal movie not found; it stays on its last tab");
					}
					return;
				}
				std::array<RE::GFxValue, 2> args{ RE::GFxValue(2.0), RE::GFxValue(false) };
				const bool                  ok = menu.Invoke("RestoreSavedSettings", nullptr, args.data(), args.size());
				logger::debug("Frontend: Journal to its System tab -> {}", ok ? "done" : "call failed");
				if (ok) {
					RunAfter(kJournalTabSettle, []() { OpenCharacterList(); });
				}
			});
		}
	}

	void OpenLoadMenu()
	{
		logger::info("Frontend: opening the vanilla load list (Journal)");
		// Loading or quitting from there leaves the lobby without our own load path.
		lobby::OnLoadListOpened();
		lobby::ClearPresets();
		lobby::UnlinkSeats();  // linked again if the Journal closes without a load
		if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(RE::JournalMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
		}
		RunAfter(std::chrono::milliseconds(150), []() { DriveJournal(0); });
	}

	void Quit()
	{
		logger::info("Frontend: quit to desktop");
		lobby::BeginLeaving("quit");
		lobby::Teardown();
		if (auto* main = RE::Main::GetSingleton()) {
			main->GetRuntimeData().quitGame = true;
		}
	}
}
