// The input and menu event sinks. Key presses become select screen actions
// (see core/InputMap.h); menu events drive the lobby and the main menu
// replacement.

#include "Events.h"

#include "Config.h"
#include "Frontend.h"
#include "Lobby.h"
#include "SelectMenu.h"
#include "Sidecar.h"
#include "Util.h"
#include "core/InputMap.h"
#include "core/UiRules.h"

namespace charselect::events
{
	namespace
	{
		using namespace input::key;

		// Editor panel: holding a move key or the mouse button repeats it.
		charselect::ui::KeyRepeat g_repeat;  // input thread only

		bool RepeatDue(const RE::ButtonEvent& a_button)
		{
			const auto id = (static_cast<std::uint32_t>(a_button.GetDevice()) << 16) | a_button.GetIDCode();
			return g_repeat.Due(id, a_button.HeldDuration());
		}

		// Menus opened over ours get the input: the Journal (LOAD, Settings), message
		// boxes, the console (typing in it drove the select screen) and
		// RaceMenu. Not "any modal menu": a mod's always-open overlay flagged
		// modal would lock the lobby.
		bool OtherMenuOnTop()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui && (ui->IsMenuOpen(RE::Console::MENU_NAME) || ui->IsMenuOpen(RE::JournalMenu::MENU_NAME) ||
							 ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) || ui->IsMenuOpen(RE::RaceSexMenu::MENU_NAME));
		}

		input::Device DeviceOf(const RE::ButtonEvent& a_button)
		{
			switch (a_button.GetDevice()) {
			case RE::INPUT_DEVICE::kKeyboard:
				return input::Device::kKeyboard;
			case RE::INPUT_DEVICE::kMouse:
				return input::Device::kMouse;
			case RE::INPUT_DEVICE::kGamepad:
				return input::Device::kGamepad;
			default:
				return input::Device::kOther;
			}
		}

		input::Context CurrentContext()
		{
			auto* ui = RE::UI::GetSingleton();
			return { .state = lobby::GetState(),
				.selectMenuOpen = SelectMenu::IsOpen(),
				.revealed = lobby::IsRevealed(),
				.menuFading = SelectMenu::IsFading(),
				.editorOpen = lobby::IsEditorOpen(),
				.atMainMenu = ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME),
				.otherMenuOnTop = OtherMenuOnTop(),
				.lobbyHotkey = config::Get().lobbyHotkey };
		}

		class InputSink : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static InputSink* GetSingleton()
			{
				static InputSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
			{
				Guarded("Input event", [&]() { Handle(a_event); });
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			void Handle(RE::InputEvent* const* a_event)
			{
				for (auto* event = a_event ? *a_event : nullptr; event; event = event->next) {
					// The legends follow whatever was used last.
					const auto type = event->GetEventType();
					if (type == RE::INPUT_EVENT_TYPE::kThumbstick || type == RE::INPUT_EVENT_TYPE::kMouseMove ||
						(type == RE::INPUT_EVENT_TYPE::kButton && event->AsButtonEvent()->IsPressed())) {
						SelectMenu::SetGamepad(event->GetDevice() == RE::INPUT_DEVICE::kGamepad);
					}
					if (type != RE::INPUT_EVENT_TYPE::kButton) {
						continue;
					}
					const auto* button = event->AsButtonEvent();
					if (!button || !button->IsPressed()) {
						continue;
					}
					if (button->IsDown()) {
						g_repeat.Reset((static_cast<std::uint32_t>(button->GetDevice()) << 16) | button->GetIDCode());
						HandleButton(*button);
					} else if (lobby::IsEditorOpen() && RepeatDue(*button)) {
						HandleEditorRepeat(*button);
					}
				}
			}

			static void HandleEditorRepeat(const RE::ButtonEvent& a_button)
			{
				if (!input::RepeatReachesEditor(CurrentContext())) {
					return;
				}
				const auto device = DeviceOf(a_button);
				if (device == input::Device::kMouse && a_button.GetIDCode() == kMouseLeft) {
					SelectMenu::QueueClick(true);
				} else if (auto key = input::MapEditKey(device, a_button.GetIDCode()); key && input::IsRepeatable(*key)) {
					SelectMenu::QueueEdit(*key);
				}
			}

			static void HandleButton(const RE::ButtonEvent& a_button)
			{
				using Kind = input::Action::Kind;
				const auto ctx = CurrentContext();
				const auto action = input::Route(ctx, DeviceOf(a_button), a_button.GetIDCode());
				switch (action.kind) {
				case Kind::kNone:
					break;
				case Kind::kEnterLobby:
					logger::info("Input: lobby hotkey at main menu");
					QueueTask([]() { frontend::EnterFromHotkey(); });
					break;
				case Kind::kSummon:
					logger::info("Input: lobby hotkey in lobby, re-applying layout");
					QueueTask([]() { lobby::Summon(); });
					break;
				case Kind::kToggleEditor:
					if (ctx.editorOpen) {
						SelectMenu::QueueEdit(SelectMenu::EditKey::kSaveClose);
					} else {
						QueueTask([]() { lobby::OpenEditor(); });
					}
					break;
				case Kind::kClick:
					SelectMenu::QueueClick();
					break;
				case Kind::kSelectSlot:
					SelectMenu::QueueSelect(action.slot);
					break;
				case Kind::kEdit:
					SelectMenu::QueueEdit(action.edit);
					break;
				case Kind::kNav:
					SelectMenu::QueueNav(action.nav);
					break;
				}
			}
		};

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuSink* GetSingleton()
			{
				static MenuSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				std::scoped_lock lock(GameLock());
				Guarded("Menu event", [&]() { Handle(a_event); });
				return RE::BSEventNotifyControl::kContinue;
			}

		private:
			void Handle(const RE::MenuOpenCloseEvent* a_event)
			{
				if (!a_event) {
					return;
				}
				const std::string_view menu = a_event->menuName.c_str();
				const bool             opening = a_event->opening;
				if (menu == RE::LoadingMenu::MENU_NAME || menu == RE::MainMenu::MENU_NAME || menu == RE::RaceSexMenu::MENU_NAME ||
					menu == SelectMenu::MENU_NAME || menu == RE::JournalMenu::MENU_NAME || menu == RE::MessageBoxMenu::MENU_NAME) {
					logger::debug("Menu: {} {}", menu, opening ? "opened" : "closed");
				} else if (lobby::GetState() != lobby::State::kIdle && menu != RE::CursorMenu::MENU_NAME && menu != RE::FaderMenu::MENU_NAME &&
						   menu != RE::HUDMenu::MENU_NAME) {
					// Mods' own menus opening at new-game start could cover the
					// lobby or take its input; log them so we know which.
					logger::debug("Menu: '{}' {} while the lobby is {}", menu, opening ? "opened" : "closed", lobby::StateName(lobby::GetState()));
				}

				if (menu == RE::MainMenu::MENU_NAME) {
					if (opening) {
						// Here, not in a task: tasks can wait seconds at
						// startup, with the vanilla menu on screen meanwhile.
						frontend::CoverMainMenu();
					}
					QueueTask(opening ? std::function<void()>(frontend::OnMainMenuOpened) : std::function<void()>(frontend::OnMainMenuClosed));
				} else if (menu == RE::JournalMenu::MENU_NAME && !opening) {
					QueueTask([]() { lobby::OnJournalClosed(); });
				} else if (menu == RE::LoadingMenu::MENU_NAME && opening) {
					lobby::OnLoadingMenuOpened();
					if (lobby::GetState() == lobby::State::kLoadingSave) {
						// PLAY: the select menu stayed up, black, until the loading screen took over.
						QueueTask([]() { SelectMenu::Hide(); });
					}
				} else if (menu == RE::LoadingMenu::MENU_NAME && !opening) {
					QueueTask([]() { lobby::OnLoadingMenuClosed(); });
				} else if (menu == RE::RaceSexMenu::MENU_NAME && !opening) {
					sidecar::OnRaceMenuClosed();
				}
			}
		};

		// Actors arriving in the lobby (an NPC's schedule bringing them in)
		// are hidden as soon as their 3D loads, not at the next sweep.
		class ObjectLoadedSink : public RE::BSTEventSink<RE::TESObjectLoadedEvent>
		{
		public:
			static ObjectLoadedSink* GetSingleton()
			{
				static ObjectLoadedSink singleton;
				return &singleton;
			}

			RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* a_event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
			{
				// Cheap filter here; the lookup and Disable() run as a task,
				// not inside the engine's 3D attach.
				Guarded("Object loaded event", [&]() {
					if (a_event && a_event->loaded && lobby::GetState() == lobby::State::kActive) {
						const auto formID = a_event->formID;
						QueueTask([formID]() { lobby::OnObjectLoaded(formID); });
					}
				});
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Register()
	{
		if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
			input->AddEventSink<RE::InputEvent*>(InputSink::GetSingleton());
			logger::debug("Events: input sink registered (lobby hotkey scan code {:#04x})", config::Get().lobbyHotkey);
		} else {
			logger::error("Events: BSInputDeviceManager unavailable, no hotkeys");
		}
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
			logger::debug("Events: menu sink registered");
		} else {
			logger::error("Events: UI unavailable, no menu events");
		}
		if (auto* scripts = RE::ScriptEventSourceHolder::GetSingleton()) {
			scripts->AddEventSink<RE::TESObjectLoadedEvent>(ObjectLoadedSink::GetSingleton());
			logger::debug("Events: object loaded sink registered");
		} else {
			logger::error("Events: ScriptEventSourceHolder unavailable; arrivals in the lobby are only caught by the sweep");
		}
	}
}
