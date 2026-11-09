// The select screen, drawn from code through Scaleform: name plates, buttons,
// the button legend, the loading splash and the layout editor panel (see
// SelectMenu.h).

#include "SelectMenu.h"

#include "Frontend.h"
#include "Lobby.h"
#include "Util.h"
#include "core/UiRules.h"

namespace charselect
{
	namespace
	{
		constexpr float kStageW = 1280.0f;
		constexpr float kStageH = 720.0f;

		// Skyrim's own menus: white for focus, greys otherwise, no colour.
		constexpr double kWhite = 0xFFFFFF;
		constexpr double kText = 0xD6D2CA;
		constexpr double kDim = 0x8E8A82;
		constexpr double kBlack = 0x000000;
		constexpr double kFaint = 0x55524C;  // disabled

		enum class Mode
		{
			kSplash,
			kSelect,
			kEdit,
		};

		// Editor step sizes: distance in game units, turn in degrees.
		struct Step
		{
			const char* name;
			float       distance;
			float       degrees;
		};
		constexpr std::array<Step, 3> kSteps{ { { "FINE", 2.0f, 1.0f }, { "NORMAL", 10.0f, 5.0f }, { "COARSE", 40.0f, 15.0f } } };

		struct Hit
		{
			float                 x, y, w, h;
			std::string           id;
			std::function<void()> action;

			[[nodiscard]] bool Contains(float a_x, float a_y) const
			{
				return a_x >= x && a_x <= x + w && a_y >= y && a_y <= y + h;
			}
		};

		// Shared between the game side (mode changes and input queueing, from
		// tasks and event sinks) and the UI thread (AdvanceMovie). Everything
		// else in this file is UI-thread only.
		struct Shared
		{
			std::mutex                    lock;
			Mode                          mode{ Mode::kSplash };
			std::string                   splashText;
			std::vector<lobby::SlotView>  slots;
			bool                          dirty{ true };
			int                           clicks{ 0 };
			int                           repeatClicks{ 0 };
			std::vector<SelectMenu::Nav>  navs;
			std::optional<std::size_t>    selectRequest;
			std::optional<std::size_t>    highlighted;  // ShowSelect: the lobby's lit character, to start on
			std::vector<SelectMenu::EditKey> editKeys;
			bool                          gamepad{ false };  // last input came from a gamepad
			bool                          reshow{ false };   // a Show*() call: ends a fade-out blackout
		} g_shared;

		// UI-thread state.
		struct View
		{
			Mode                         mode{ Mode::kSplash };
			std::string                  splashText;
			std::vector<lobby::SlotView> slots;
			std::size_t                  selected{ 0 };
			std::vector<std::size_t>     order;  // slot indices left to right as they stand
			std::string                  hover;
			std::vector<Hit>             hits;        // UI elements, topmost last
			std::vector<Hit>             actorHits;   // projected characters
			std::size_t                  plateCount{ 0 };  // name plates built (_root.ui.plates.plate<i>)
			std::optional<std::size_t>   menuFocus;   // keyboard or gamepad on the NEW GAME / LOAD / MAIN MENU / QUIT list
			float                        cursorX{ -1.0f };
			float                        cursorY{ -1.0f };
			bool                         needsBuild{ true };
			float                        fade{ 0.0f };  // seconds left of the splash fading out
			bool                         spinnerBuilt{ false };  // _root.ui.spinner exists
			float                        spinTime{ 0.0f };
			bool                         gamepad{ false };  // legends show gamepad buttons
			// Fade to black before leaving (Play, New Game, Main Menu, Quit).
			float                        fadeOut{ -1.0f };   // seconds elapsed; < 0 when not fading
			bool                         blackout{ false };  // faded out; stays black until the next screen
			std::function<void()>        afterFade;
			// editor
			std::size_t                     editTarget{ 0 };  // 0 camera, i+1 slot i
			std::size_t                     step{ 1 };
			std::vector<lobby::EditTarget>  editTargets;
			bool                            resetArmed{ false };
		} g_view;

		constexpr float kSplashFade = 0.8f;

		std::atomic<bool> g_movieFailed{ false };  // the SWF didn't load this session
		std::atomic<bool> g_fading{ false };       // Leaving(), for the input thread

		// Vanilla menu sounds (the names Skyrim's own menus use).
		void Sound(const char* a_name)
		{
			QueueTask([a_name]() { RE::PlaySound(a_name); });
		}

		constexpr float kFadeOut = 0.45f;

		bool Leaving()
		{
			return g_view.fadeOut >= 0.0f || g_view.blackout;
		}

		// Fades to black, then runs a_then (UI thread) and stays black until
		// the next screen replaces this one.
		void FadeThen(std::function<void()> a_then)
		{
			if (Leaving()) {
				return;
			}
			g_view.fadeOut = 0.0f;
			g_view.afterFade = std::move(a_then);
			g_fading = true;
		}

		// ---- GFx helpers ---------------------------------------------------

		class Ui
		{
		public:
			explicit Ui(RE::GFxMovieView* a_movie) :
				_movie(a_movie) {}

			RE::GFxValue Clip(RE::GFxValue& a_parent, const char* a_name, int a_depth)
			{
				std::array<RE::GFxValue, 2> args{ RE::GFxValue(a_name), RE::GFxValue(static_cast<double>(a_depth)) };
				RE::GFxValue                out;
				a_parent.Invoke("createEmptyMovieClip", &out, args);
				if (!out.IsObject()) {
					a_parent.GetMember(a_name, &out);
				}
				return out;
			}

			void Rect(RE::GFxValue& a_clip, float a_x, float a_y, float a_w, float a_h, double a_color, double a_alpha,
				double a_line = -1.0, double a_lineAlpha = 100.0, double a_thickness = 1.0)
			{
				if (a_line >= 0.0) {
					std::array<RE::GFxValue, 3> ls{ RE::GFxValue(a_thickness), RE::GFxValue(a_line), RE::GFxValue(a_lineAlpha) };
					a_clip.Invoke("lineStyle", nullptr, ls);
				} else {
					a_clip.Invoke("lineStyle", nullptr, std::array<RE::GFxValue, 0>{});
				}
				std::array<RE::GFxValue, 2> fill{ RE::GFxValue(a_color), RE::GFxValue(a_alpha) };
				a_clip.Invoke("beginFill", nullptr, fill);
				MoveTo(a_clip, a_x, a_y);
				LineTo(a_clip, a_x + a_w, a_y);
				LineTo(a_clip, a_x + a_w, a_y + a_h);
				LineTo(a_clip, a_x, a_y + a_h);
				LineTo(a_clip, a_x, a_y);
				a_clip.Invoke("endFill");
			}

			// a_heading uses Futura Condensed Medium ($EverywhereMediumFont), the
			// face Skyrim uses for menu headings and entries.
			RE::GFxValue Text(RE::GFxValue& a_parent, const std::string& a_name, int a_depth, float a_x, float a_y, float a_w, float a_h,
				const std::string& a_text, double a_size, double a_color, const char* a_align = "left", bool a_heading = false, double a_spacing = 0.0)
			{
				std::array<RE::GFxValue, 6> args{
					RE::GFxValue(a_name.c_str()), RE::GFxValue(static_cast<double>(a_depth)),
					RE::GFxValue(static_cast<double>(a_x)), RE::GFxValue(static_cast<double>(a_y)),
					RE::GFxValue(static_cast<double>(a_w)), RE::GFxValue(static_cast<double>(a_h))
				};
				a_parent.Invoke("createTextField", nullptr, args);
				RE::GFxValue tf;
				a_parent.GetMember(a_name.c_str(), &tf);
				if (!tf.IsObject()) {
					return tf;
				}
				tf.SetMember("embedFonts", RE::GFxValue(true));
				tf.SetMember("selectable", RE::GFxValue(false));
				tf.SetMember("multiline", RE::GFxValue(false));
				tf.SetMember("wordWrap", RE::GFxValue(false));

				RE::GFxValue fmt;
				_movie->CreateObject(&fmt, "TextFormat");
				if (fmt.IsObject()) {
					fmt.SetMember("font", RE::GFxValue(a_heading ? "$EverywhereMediumFont" : "$EverywhereFont"));
					fmt.SetMember("size", RE::GFxValue(a_size));
					if (a_spacing != 0.0) {
						fmt.SetMember("letterSpacing", RE::GFxValue(a_spacing));
					}
					fmt.SetMember("color", RE::GFxValue(a_color));
					fmt.SetMember("align", RE::GFxValue(a_align));
					tf.Invoke("setNewTextFormat", nullptr, &fmt, 1);
				}
				tf.SetMember("text", RE::GFxValue(a_text.c_str()));
				return tf;
			}

			// Rectangle filled with a linear gradient (AS2 beginGradientFill). Stops
			// are (alpha 0-100, ratio 0-255) of a single colour; vertical runs top
			// to bottom, otherwise left to right.
			void Gradient(RE::GFxValue& a_clip, float a_x, float a_y, float a_w, float a_h, double a_color,
				std::initializer_list<std::pair<double, double>> a_stops, bool a_vertical)
			{
				RE::GFxValue colors, alphas, ratios, matrix;
				_movie->CreateArray(&colors);
				_movie->CreateArray(&alphas);
				_movie->CreateArray(&ratios);
				for (const auto& [alpha, ratio] : a_stops) {
					colors.PushBack(RE::GFxValue(a_color));
					alphas.PushBack(RE::GFxValue(alpha));
					ratios.PushBack(RE::GFxValue(ratio));
				}
				_movie->CreateObject(&matrix);
				matrix.SetMember("matrixType", RE::GFxValue("box"));
				matrix.SetMember("x", RE::GFxValue(static_cast<double>(a_x)));
				matrix.SetMember("y", RE::GFxValue(static_cast<double>(a_y)));
				matrix.SetMember("w", RE::GFxValue(static_cast<double>(a_w)));
				matrix.SetMember("h", RE::GFxValue(static_cast<double>(a_h)));
				matrix.SetMember("r", RE::GFxValue(a_vertical ? 1.5707963267948966 : 0.0));

				a_clip.Invoke("lineStyle", nullptr, std::array<RE::GFxValue, 0>{});
				std::array<RE::GFxValue, 5> args{ RE::GFxValue("linear"), colors, alphas, ratios, matrix };
				a_clip.Invoke("beginGradientFill", nullptr, args);
				MoveTo(a_clip, a_x, a_y);
				LineTo(a_clip, a_x + a_w, a_y);
				LineTo(a_clip, a_x + a_w, a_y + a_h);
				LineTo(a_clip, a_x, a_y + a_h);
				LineTo(a_clip, a_x, a_y);
				a_clip.Invoke("endFill");
			}

			// Thin horizontal rule that fades out towards both ends, like the
			// dividers in Skyrim's menus.
			void Rule(RE::GFxValue& a_clip, float a_cx, float a_y, float a_w, double a_alpha, double a_color = 0xFFFFFF)
			{
				Gradient(a_clip, a_cx - a_w / 2.0f, a_y, a_w, 1.0f, a_color, { { 0.0, 0.0 }, { a_alpha, 127.0 }, { 0.0, 255.0 } }, false);
			}

			// Filters on a clip: always a soft dark halo so text reads over bright
			// scenery (as Skyrim's HUD does), plus an optional coloured glow for
			// focus. Uses flash.filters.GlowFilter; skipped if unavailable.
			void Glow(RE::GFxValue& a_clip, double a_color, double a_alpha, double a_blur, double a_strength)
			{
				RE::GFxValue list;
				_movie->CreateArray(&list);
				const auto add = [&](double a_c, double a_a, double a_b, double a_s) {
					std::array<RE::GFxValue, 6> args{ RE::GFxValue(a_c), RE::GFxValue(a_a), RE::GFxValue(a_b), RE::GFxValue(a_b),
						RE::GFxValue(a_s), RE::GFxValue(2.0) };
					RE::GFxValue filter;
					_movie->CreateObject(&filter, "flash.filters.GlowFilter", args.data(), static_cast<std::uint32_t>(args.size()));
					if (filter.IsObject()) {
						list.PushBack(filter);
					}
				};
				add(0x000000, 0.75, 6.0, 1.6);
				if (a_alpha > 0.0) {
					add(a_color, a_alpha, a_blur, a_strength);
				}
				a_clip.SetMember("filters", list);
			}

			void Shadow(RE::GFxValue& a_clip)
			{
				Glow(a_clip, 0, 0.0, 0.0, 0.0);
			}

			// Filled triangle pointing up (0), right (1), down (2) or left (3).
			void Triangle(RE::GFxValue& a_clip, float a_cx, float a_cy, float a_size, int a_dir, double a_color, double a_alpha)
			{
				const std::array<std::pair<float, float>, 3> up{ { { 0.0f, -a_size }, { a_size, a_size * 0.7f }, { -a_size, a_size * 0.7f } } };
				const float c = std::cos(static_cast<float>(a_dir) * RE::NI_HALF_PI);
				const float sn = std::sin(static_cast<float>(a_dir) * RE::NI_HALF_PI);
				a_clip.Invoke("lineStyle", nullptr, std::array<RE::GFxValue, 0>{});
				std::array<RE::GFxValue, 2> fill{ RE::GFxValue(a_color), RE::GFxValue(a_alpha) };
				a_clip.Invoke("beginFill", nullptr, fill);
				for (std::size_t i = 0; i <= up.size(); ++i) {
					const auto [x, y] = up[i % up.size()];
					const float px = a_cx + x * c - y * sn;
					const float py = a_cy + x * sn + y * c;
					if (i == 0) {
						MoveTo(a_clip, px, py);
					} else {
						LineTo(a_clip, px, py);
					}
				}
				a_clip.Invoke("endFill");
			}

			// A ring of ticks around (0, 0), brightest at the top and fading
			// anticlockwise, so turning the clip clockwise reads as a spinner.
			void Spinner(RE::GFxValue& a_clip, float a_radius, double a_color)
			{
				constexpr int kTicks = 12;
				for (int i = 0; i < kTicks; ++i) {
					const float  angle = -static_cast<float>(i) * RE::NI_TWO_PI / kTicks;
					const double alpha = 100.0 - 85.0 * i / (kTicks - 1);
					std::array<RE::GFxValue, 3> ls{ RE::GFxValue(2.0), RE::GFxValue(a_color), RE::GFxValue(alpha) };
					a_clip.Invoke("lineStyle", nullptr, ls);
					MoveTo(a_clip, std::sin(angle) * a_radius * 0.5f, -std::cos(angle) * a_radius * 0.5f);
					LineTo(a_clip, std::sin(angle) * a_radius, -std::cos(angle) * a_radius);
				}
			}

			// Filled circle (eight quadratic curves), with an outline.
			void Circle(RE::GFxValue& a_clip, float a_cx, float a_cy, float a_r, double a_fill, double a_fillAlpha, double a_line, double a_lineAlpha)
			{
				std::array<RE::GFxValue, 3> ls{ RE::GFxValue(1.5), RE::GFxValue(a_line), RE::GFxValue(a_lineAlpha) };
				a_clip.Invoke("lineStyle", nullptr, ls);
				std::array<RE::GFxValue, 2> fill{ RE::GFxValue(a_fill), RE::GFxValue(a_fillAlpha) };
				a_clip.Invoke("beginFill", nullptr, fill);
				constexpr int kSegments = 8;
				const float   step = RE::NI_TWO_PI / kSegments;
				const float   control = a_r / std::cos(step / 2.0f);
				MoveTo(a_clip, a_cx + a_r, a_cy);
				for (int i = 1; i <= kSegments; ++i) {
					const float a = step * static_cast<float>(i);
					const float c = a - step / 2.0f;
					std::array<RE::GFxValue, 4> p{ RE::GFxValue(static_cast<double>(a_cx + std::cos(c) * control)),
						RE::GFxValue(static_cast<double>(a_cy + std::sin(c) * control)), RE::GFxValue(static_cast<double>(a_cx + std::cos(a) * a_r)),
						RE::GFxValue(static_cast<double>(a_cy + std::sin(a) * a_r)) };
					a_clip.Invoke("curveTo", nullptr, p);
				}
				a_clip.Invoke("endFill");
			}

			// Width of the text in a field made by Text(); a rough estimate from
			// the character count if GFx doesn't say.
			static float TextWidth(RE::GFxValue& a_field)
			{
				RE::GFxValue width, text;
				if (a_field.IsObject() && a_field.GetMember("textWidth", &width) && width.IsNumber() && width.GetNumber() > 0.0) {
					return static_cast<float>(width.GetNumber());
				}
				const bool known = a_field.IsObject() && a_field.GetMember("text", &text) && text.IsString();
				return known ? static_cast<float>(std::strlen(text.GetString())) * 6.5f : 0.0f;
			}

			// The whole visible area in stage coordinates: wider or taller than
			// the 1280x720 stage when the screen's aspect differs (kShowAll).
			[[nodiscard]] RE::GRectF Visible() const { return _movie->GetVisibleFrameRect(); }

		private:
			static void MoveTo(RE::GFxValue& a_clip, float a_x, float a_y)
			{
				std::array<RE::GFxValue, 2> p{ RE::GFxValue(static_cast<double>(a_x)), RE::GFxValue(static_cast<double>(a_y)) };
				a_clip.Invoke("moveTo", nullptr, p);
			}
			static void LineTo(RE::GFxValue& a_clip, float a_x, float a_y)
			{
				std::array<RE::GFxValue, 2> p{ RE::GFxValue(static_cast<double>(a_x)), RE::GFxValue(static_cast<double>(a_y)) };
				a_clip.Invoke("lineTo", nullptr, p);
			}

			RE::GFxMovieView* _movie;
		};

		// ---- actions ---------------------------------------------------------

		void Select(std::size_t a_slot)
		{
			if (a_slot < g_view.slots.size() && a_slot != g_view.selected) {
				g_view.selected = a_slot;
				g_view.needsBuild = true;
				logger::debug("SelectMenu: selected slot {}", a_slot + 1);
				QueueTask([a_slot]() { lobby::Highlight(a_slot); });
			}
		}

		void DoPlay()
		{
			if (g_view.selected >= g_view.slots.size()) {
				return;
			}
			const auto slot = g_view.selected;
			logger::info("SelectMenu: Play slot {} '{}'", slot + 1, g_view.slots[slot].name);
			// The menu stays up (black) until the loading screen takes over.
			FadeThen([slot]() { QueueTask([slot]() { lobby::Play(slot); }); });
		}

		void OpenLoad()
		{
			logger::info("SelectMenu: Load (vanilla)");
			QueueTask([]() { frontend::OpenLoadMenu(); });
		}

		// The main menu actions, bottom right. Everything but ENTER is
		// vanilla's own UI. (No Settings: they're often per save, and here
		// they'd be the lobby's.)
		struct MenuItem
		{
			const char* id;
			const char* label;
			void (*action)();
		};
		const std::array<MenuItem, 4> kMenuItems{ {
			{ "newgame", "NEW GAME", []() { FadeThen([]() { QueueTask([]() { frontend::ReturnToMainMenu(frontend::Action::kNewGame); }); }); } },
			{ "load", "LOAD", []() { OpenLoad(); } },
			{ "mainmenu", "MAIN MENU", []() { FadeThen([]() { QueueTask([]() { frontend::ReturnToMainMenu(frontend::Action::kClassicMenu); }); }); } },
			{ "quit", "QUIT", []() { FadeThen([]() { QueueTask([]() { frontend::Quit(); }); }); } },
		} };

		void FocusMenu(std::optional<std::size_t> a_item)
		{
			if (a_item != g_view.menuFocus) {
				g_view.menuFocus = a_item;
				g_view.needsBuild = true;
				Sound("UIMenuFocus");
			}
		}

		// Left and right choose a character. Down moves onto the menu list,
		// up and down move through it, and up from its top entry (or B) goes
		// back to the characters.
		void HandleNav(SelectMenu::Nav a_nav)
		{
			using Nav = SelectMenu::Nav;
			if (g_view.mode != Mode::kSelect) {
				return;
			}
			const auto focus = g_view.menuFocus;
			switch (a_nav) {
			case Nav::kLeft:
			case Nav::kRight:
				if (focus) {
					g_view.menuFocus.reset();
					g_view.needsBuild = true;
				}
				if (const auto next = charselect::ui::StepSelection(g_view.order, g_view.selected, a_nav == Nav::kLeft ? -1 : 1)) {
					Sound("UIMenuFocus");
					Select(*next);
				}
				break;
			case Nav::kDown:
				FocusMenu(!focus ? 0 : (std::min)(*focus + 1, kMenuItems.size() - 1));
				break;
			case Nav::kUp:
				if (focus) {
					FocusMenu(*focus == 0 ? std::nullopt : std::optional(*focus - 1));
				}
				break;
			case Nav::kBack:
				if (focus) {
					FocusMenu(std::nullopt);
				}
				break;
			case Nav::kConfirm:
				Sound("UIMenuOK");
				if (focus) {
					logger::info("SelectMenu: {}", kMenuItems[*focus].label);
					kMenuItems[*focus].action();
				} else {
					DoPlay();
				}
				break;
			case Nav::kSaves:
				Sound("UIMenuOK");
				OpenLoad();
				break;
			}
		}

		// ---- editor actions (UI thread) ----------------------------------------

		void SetEditTarget(std::size_t a_target)
		{
			if (a_target < g_view.editTargets.size() && a_target != g_view.editTarget) {
				g_view.editTarget = a_target;
				g_view.resetArmed = false;
				g_view.needsBuild = true;
				QueueTask([a_target]() { lobby::EditFocus(a_target); });
			}
		}

		void Nudge(float a_forward, float a_right, float a_up, float a_turnDeg, float a_tiltDeg)
		{
			const auto target = g_view.editTarget;
			if (target >= g_view.editTargets.size() || !g_view.editTargets[target].movable) {
				return;
			}
			g_view.resetArmed = false;
			const float turn = a_turnDeg * RE::NI_PI / 180.0f;
			const float tilt = a_tiltDeg * RE::NI_PI / 180.0f;
			QueueTask([=]() { lobby::EditNudge(target, a_forward, a_right, a_up, turn, tilt); });
		}

		void EditCycle(lobby::EditField a_field, int a_dir)
		{
			const auto target = g_view.editTarget;
			if (target == 0 || target >= g_view.editTargets.size()) {
				return;
			}
			g_view.resetArmed = false;
			QueueTask([=]() { lobby::EditCycle(target, a_field, a_dir); });
		}

		void EditSaveClose()
		{
			QueueTask([]() { lobby::CloseEditor(true); });
		}

		void EditCancel()
		{
			QueueTask([]() { lobby::CloseEditor(false); });
		}

		void HandleEditKey(SelectMenu::EditKey a_key)
		{
			using Key = SelectMenu::EditKey;
			const auto& step = kSteps[g_view.step];
			const bool  camera = g_view.editTarget == 0;
			switch (a_key) {
			case Key::kForward:
				Nudge(step.distance, 0, 0, 0, 0);
				break;
			case Key::kBack:
				Nudge(-step.distance, 0, 0, 0, 0);
				break;
			case Key::kLeft:
				Nudge(0, -step.distance, 0, 0, 0);
				break;
			case Key::kRight:
				Nudge(0, step.distance, 0, 0, 0);
				break;
			case Key::kRaise:
				Nudge(0, 0, step.distance, 0, 0);
				break;
			case Key::kLower:
				Nudge(0, 0, -step.distance, 0, 0);
				break;
			case Key::kTurnLeft:
				Nudge(0, 0, 0, -step.degrees, 0);
				break;
			case Key::kTurnRight:
				Nudge(0, 0, 0, step.degrees, 0);
				break;
			case Key::kTiltUp:
				if (camera) {
					Nudge(0, 0, 0, 0, -step.degrees);
				}
				break;
			case Key::kTiltDown:
				if (camera) {
					Nudge(0, 0, 0, 0, step.degrees);
				}
				break;
			case Key::kNextTarget:
				if (!g_view.editTargets.empty()) {
					Sound("UIMenuFocus");
					SetEditTarget((g_view.editTarget + 1) % g_view.editTargets.size());
				}
				break;
			case Key::kCamera:
				Sound("UIMenuFocus");
				SetEditTarget(0);
				break;
			case Key::kSaveClose:
				Sound("UIMenuOK");
				EditSaveClose();
				break;
			case Key::kCancel:
				Sound("UIMenuCancel");
				EditCancel();
				break;
			}
		}

		// ---- layout ----------------------------------------------------------

		// Vertical black fade.
		void Fade(Ui& a_ui, RE::GFxValue& a_clip, float a_x, float a_y, float a_w, float a_h, double a_alphaFrom, double a_alphaTo)
		{
			a_ui.Gradient(a_clip, a_x, a_y, a_w, a_h, kBlack, { { a_alphaFrom, 0.0 }, { a_alphaTo, 255.0 } }, true);
		}

		// Horizontal black fade (behind lists, like the vanilla load menu).
		void SideFade(Ui& a_ui, RE::GFxValue& a_clip, float a_w, double a_alphaFrom, double a_alphaTo)
		{
			a_ui.Gradient(a_clip, 0, 0, a_w, kStageH, kBlack, { { a_alphaFrom, 0.0 }, { a_alphaTo, 255.0 } }, false);
		}

		// Between the parts of a character's info line.
		constexpr const char* kDot = "   \xC2\xB7   ";  // spaced middle dot (UTF-8)

		// Plain text entry: grey, white with a soft glow on hover or focus, Skyrim menu style.
		void AddLink(Ui& a_ui, RE::GFxValue& a_parent, int& a_depth, const std::string& a_id, const std::string& a_label,
			float a_x, float a_y, float a_w, double a_size, const char* a_align, std::function<void()> a_action, bool a_focused = false)
		{
			const bool  hover = a_focused || g_view.hover == a_id;
			const float h = static_cast<float>(a_size * 1.7);
			auto        clip = a_ui.Clip(a_parent, ("link_" + a_id).c_str(), a_depth++);
			a_ui.Text(clip, "label", 1, a_x, a_y, a_w, h, a_label, a_size, hover ? kWhite : kDim, a_align, true, 1.0);
			if (hover) {
				a_ui.Glow(clip, kWhite, 0.35, 6.0, 1.2);
			} else {
				a_ui.Shadow(clip);
			}
			g_view.hits.push_back({ a_x, a_y, a_w, h, a_id, std::move(a_action) });
		}

		// ---- button legend ----------------------------------------------------

		// One entry: key glyphs, then what they do. A glyph is a key's text
		// in a keycap ("Enter", "F8"), "<" / ">" for arrow keys, "dpad", or
		// "pad:A" / "pad:B" / "pad:X" / "pad:Y" for gamepad buttons.
		struct LegendItem
		{
			std::vector<std::string> keys;
			std::string              label;
		};

		constexpr float kGlyphH = 18.0f;

		// Draws one glyph with its left edge at a_x, centred on a_cy; returns its width.
		float LegendGlyph(Ui& a_ui, RE::GFxValue& a_parent, int& a_depth, const std::string& a_key, float a_x, float a_cy)
		{
			auto clip = a_ui.Clip(a_parent, std::format("g{}", a_depth).c_str(), a_depth);
			++a_depth;
			if (a_key.starts_with("pad:")) {
				// Xbox colours, like Skyrim's own gamepad prompts.
				const char  letter = a_key.size() > 4 ? a_key[4] : 'A';
				const double color = letter == 'A' ? 0x6DBE45 : letter == 'B' ? 0xE0533F : letter == 'X' ? 0x3F8FDD : 0xE9B93A;
				const float  r = kGlyphH / 2.0f;
				a_ui.Circle(clip, a_x + r, a_cy, r, kBlack, 55.0, color, 90.0);
				a_ui.Text(clip, "t", 1, a_x, a_cy - 9.0f, kGlyphH, 18, std::string(1, letter), 11, color, "center", true);
				return kGlyphH;
			}
			if (a_key == "dpad") {
				// A plus sign: the D-pad.
				const float s = kGlyphH, t = 6.0f, cx = a_x + s / 2.0f;
				a_ui.Rect(clip, cx - t / 2.0f, a_cy - s / 2.0f, t, s, kText, 85.0);
				a_ui.Rect(clip, a_x, a_cy - t / 2.0f, s, t, kText, 85.0);
				return s;
			}
			// Keycap: a dark box with a thin light edge; arrows as triangles.
			float w = kGlyphH;
			if (a_key == "<" || a_key == ">" || a_key == "^" || a_key == "v") {
				a_ui.Rect(clip, a_x, a_cy - kGlyphH / 2.0f, w, kGlyphH, kBlack, 55.0, kText, 60.0);
				const int dir = a_key == "^" ? 0 : a_key == ">" ? 1 : a_key == "v" ? 2 : 3;
				a_ui.Triangle(clip, a_x + w / 2.0f, a_cy, 4.0f, dir, kText, 100.0);
				return w;
			}
			auto box = a_ui.Clip(clip, "box", 1);
			auto tf = a_ui.Text(clip, "t", 2, a_x, a_cy - 9.0f, 120, 18, a_key, 10, kText, "left", true, 0.5);
			w = (std::max)(kGlyphH, Ui::TextWidth(tf) + 12.0f);
			tf.SetMember("_x", RE::GFxValue(static_cast<double>(a_x + (w - Ui::TextWidth(tf)) / 2.0f - 2.0f)));
			a_ui.Rect(box, a_x, a_cy - kGlyphH / 2.0f, w, kGlyphH, kBlack, 55.0, kText, 60.0);
			return w;
		}

		// A row of legend entries from a_x along a_cy.
		void BuildLegend(Ui& a_ui, RE::GFxValue& a_parent, const char* a_name, int a_depth, float a_x, float a_cy, const std::vector<LegendItem>& a_items)
		{
			auto  legend = a_ui.Clip(a_parent, a_name, a_depth);
			int   depth = 1;
			float x = a_x;
			for (const auto& item : a_items) {
				for (std::size_t k = 0; k < item.keys.size(); ++k) {
					x += LegendGlyph(a_ui, legend, depth, item.keys[k], x, a_cy) + (k + 1 < item.keys.size() ? 3.0f : 7.0f);
				}
				auto label = a_ui.Text(legend, std::format("l{}", depth), depth, x, a_cy - 10.0f, 300, 20, item.label, 12, kText, "left", true, 1.0);
				++depth;
				x += Ui::TextWidth(label) + 22.0f;
			}
			a_ui.Shadow(legend);
		}

		// Like Skyrim's own loading screens: black, with a small spinner and a
		// status line in the bottom-right corner (the spinner turns in
		// UpdateSpinner).
		void BuildSplash(Ui& a_ui, RE::GFxValue& a_root)
		{
			const auto vis = a_ui.Visible();
			a_ui.Rect(a_root, vis.left, vis.top, vis.right - vis.left, vis.bottom - vis.top, kBlack, 100.0);
			const float cx = vis.right - 56.0f;
			const float cy = vis.bottom - 48.0f;
			a_ui.Text(a_root, "status", 1, cx - 26.0f - 600.0f, cy - 10.0f, 600.0f, 22.0f, g_view.splashText, 13, kDim, "right", true, 1.5);
			auto spinner = a_ui.Clip(a_root, "spinner", 2);
			a_ui.Spinner(spinner, 10.0f, kText);
			spinner.SetMember("_x", RE::GFxValue(static_cast<double>(cx)));
			spinner.SetMember("_y", RE::GFxValue(static_cast<double>(cy)));
			g_view.spinnerBuilt = true;
		}

		// Name plates over the characters, positioned every frame.
		void BuildPlates(Ui& a_ui, RE::GFxValue& a_root, int& a_depth, std::optional<std::size_t> a_selected)
		{
			auto plates = a_ui.Clip(a_root, "plates", a_depth++);
			g_view.plateCount = g_view.slots.size();
			for (std::size_t i = 0; i < g_view.slots.size(); ++i) {
				const auto& sl = g_view.slots[i];
				const bool  sel = a_selected == i;
				const bool  hover = g_view.hover == std::format("actor{}", i);
				auto        plate = a_ui.Clip(plates, ("plate" + std::to_string(i)).c_str(), static_cast<int>(i + 1));
				a_ui.Text(plate, "name", 1, -150, 0, 300, 30, sl.name, sel ? 20 : 16, sel || hover ? kWhite : kDim, "center", true, 1.5);
				if (sel || hover) {
					a_ui.Glow(plate, kWhite, sel ? 0.35 : 0.2, 8.0, 1.2);
				} else {
					a_ui.Shadow(plate);
				}
				plate.SetMember("_visible", RE::GFxValue(false));
			}
		}

		// Boxed button for the editor panel.
		void AddButton(Ui& a_ui, RE::GFxValue& a_parent, int& a_depth, const std::string& a_id, const std::string& a_label, float a_x, float a_y,
			float a_w, float a_h, std::function<void()> a_action, bool a_enabled = true, bool a_active = false)
		{
			const bool hover = a_enabled && g_view.hover == a_id;
			auto       clip = a_ui.Clip(a_parent, ("btn_" + a_id).c_str(), a_depth++);
			a_ui.Rect(clip, a_x, a_y, a_w, a_h, kBlack, hover ? 60.0 : 35.0, hover || a_active ? kWhite : kDim, hover || a_active ? 85.0 : 35.0);
			const double color = !a_enabled ? kFaint : (hover || a_active ? kWhite : kText);
			a_ui.Text(clip, "label", 1, a_x, a_y + (a_h - 19.0f) / 2.0f, a_w, 19, a_label, 12, color, "center", true, 1.0);
			if (hover) {
				a_ui.Glow(clip, kWhite, 0.3, 6.0, 1.1);
			}
			if (a_enabled) {
				g_view.hits.push_back({ a_x, a_y, a_w, a_h, a_id, std::move(a_action) });
			}
		}

		// One arrow of the move pad.
		void AddArrow(Ui& a_ui, RE::GFxValue& a_parent, int& a_depth, const std::string& a_id, float a_cx, float a_cy, int a_dir,
			std::function<void()> a_action, bool a_enabled)
		{
			constexpr float half = 21.0f;
			const bool      hover = a_enabled && g_view.hover == a_id;
			auto            clip = a_ui.Clip(a_parent, ("arrow_" + a_id).c_str(), a_depth++);
			a_ui.Rect(clip, a_cx - half, a_cy - half, half * 2, half * 2, kBlack, hover ? 60.0 : 35.0, hover ? kWhite : kDim, hover ? 85.0 : 35.0);
			a_ui.Triangle(clip, a_cx, a_cy + (a_dir == 0 ? 2.0f : a_dir == 2 ? -2.0f : 0.0f), 8.0f, a_dir, !a_enabled ? kFaint : (hover ? kWhite : kText), 100.0);
			if (hover) {
				a_ui.Glow(clip, kWhite, 0.3, 6.0, 1.1);
			}
			if (a_enabled) {
				g_view.hits.push_back({ a_cx - half, a_cy - half, half * 2, half * 2, a_id, std::move(a_action) });
			}
		}

		void BuildEdit(Ui& a_ui, RE::GFxValue& a_root)
		{
			int         depth = 1;
			const float x = 950, w = 296, cx = x + w / 2.0f;
			const auto& targets = g_view.editTargets;
			if (g_view.editTarget >= targets.size()) {
				g_view.editTarget = 0;
			}
			const lobby::EditTarget* target = targets.empty() ? nullptr : &targets[g_view.editTarget];
			const bool               movable = target && target->movable;
			const bool               camera = target && target->camera;

			// Dark band behind the panel; clicks on it don't fall through to the scene.
			auto shade = a_ui.Clip(a_root, "editShade", depth++);
			a_ui.Gradient(shade, 850, 0, kStageW - 850, kStageH, kBlack, { { 0.0, 0.0 }, { 72.0, 90.0 }, { 84.0, 255.0 } }, false);
			g_view.hits.push_back({ x - 14, 0, kStageW - x + 14, kStageH, "editPanel", []() {} });

			std::optional<std::size_t> plateSel;
			if (g_view.editTarget > 0) {
				plateSel = g_view.editTarget - 1;
			}
			BuildPlates(a_ui, a_root, depth, plateSel);

			auto panel = a_ui.Clip(a_root, "editPanel", depth++);
			a_ui.Text(panel, "title", 1, x, 26, w, 36, "LAYOUT", 26, kWhite, "left", true, 5.0);
			a_ui.Shadow(panel);
			auto rules = a_ui.Clip(a_root, "editRules", depth++);
			a_ui.Rule(rules, cx, 64, w, 50.0);

			// What to move. Tighter rows for a long list, so the pose pickers still fit.
			const float rowStep = targets.size() > 6 ? 21.0f : 26.0f;
			float       y = 76;
			for (std::size_t i = 0; i < targets.size(); ++i) {
				const auto& t = targets[i];
				const auto  id = std::format("target{}", i);
				const bool  sel = i == g_view.editTarget;
				const bool  hover = g_view.hover == id;
				auto        row = a_ui.Clip(panel, ("row" + std::to_string(i)).c_str(), static_cast<int>(10 + i));
				if (sel) {
					a_ui.Rect(row, x - 6, y, w + 12, 24, 0xFFFFFF, 8.0);
				}
				a_ui.Text(row, "label", 1, x, y + 2, w * 0.62f, 22, t.label, 15, sel || hover ? kWhite : kText, "left", true, 0.5);
				a_ui.Text(row, "kind", 2, x + w * 0.62f, y + 5, w * 0.38f, 20, t.kind, 11, sel ? kText : kDim, "right");
				if (sel) {
					a_ui.Glow(row, kWhite, 0.2, 6.0, 1.0);
				}
				g_view.hits.push_back({ x - 6, y, w + 12, 24, id, [i]() { SetEditTarget(i); } });
				y += rowStep;
			}

			y += 6;
			a_ui.Rule(rules, cx, y, w, 30.0);
			y += 8;
			if (target) {
				a_ui.Text(panel, "detail", 40, x, y, w, 20, target->detail, 11, kText, "left");
				y += 18;
				if (!target->movable) {
					a_ui.Text(panel, "note", 41, x, y, w, 20, target->note, 11, kDim, "left");
					y += 18;
				}
			}

			// Pose pickers: < value > rows for a character.
			if (target && !camera) {
				y += 6;
				int        textDepth = 60;
				const auto picker = [&](const std::string& a_id, const char* a_label, const std::string& a_value, lobby::EditField a_field) {
					constexpr float kArrow = 24.0f;
					const float     vx = x + 78.0f;
					a_ui.Text(panel, a_id + "L", textDepth++, x, y + 5, 76, 18, a_label, 10, kDim, "left", true, 1.5);
					AddButton(a_ui, a_root, depth, a_id + "_prev", "<", vx, y, kArrow, kArrow, [a_field]() { EditCycle(a_field, -1); });
					a_ui.Text(panel, a_id + "V", textDepth++, vx + kArrow + 4, y + 3, w - 78 - 2 * kArrow - 8, 20, a_value, 13, kText, "center", true, 0.5);
					AddButton(a_ui, a_root, depth, a_id + "_next", ">", x + w - kArrow, y, kArrow, kArrow, [a_field]() { EditCycle(a_field, 1); });
					y += 28;
				};
				picker("rest", "POSE", target->rest, lobby::EditField::kRest);
				picker("select", "SELECTED", target->select, lobby::EditField::kSelect);
				picker("seat", "SEAT", target->seat, lobby::EditField::kSeat);
				if (!target->poseNote.empty()) {
					a_ui.Text(panel, "poseNote", textDepth++, x, y, w, 20, target->poseNote, 11, kDim, "left");
					y += 16;
				}
			}

			// Move pad: directions as seen from the camera.
			const float padY = (std::max)(y + 70.0f, 360.0f);
			const float padX = x + 70;
			const float gap = 46;
			const auto& step = kSteps[g_view.step];
			AddArrow(a_ui, a_root, depth, "fwd", padX, padY - gap, 0, []() { HandleEditKey(SelectMenu::EditKey::kForward); }, movable);
			AddArrow(a_ui, a_root, depth, "back", padX, padY + gap, 2, []() { HandleEditKey(SelectMenu::EditKey::kBack); }, movable);
			AddArrow(a_ui, a_root, depth, "left", padX - gap, padY, 3, []() { HandleEditKey(SelectMenu::EditKey::kLeft); }, movable);
			AddArrow(a_ui, a_root, depth, "right", padX + gap, padY, 1, []() { HandleEditKey(SelectMenu::EditKey::kRight); }, movable);
			{
				// Centre of the pad: the step size; click to change it.
				const bool hover = g_view.hover == "step";
				auto       clip = a_ui.Clip(a_root, "stepBox", depth++);
				a_ui.Text(clip, "a", 1, padX - 24, padY - 15, 48, 16, "STEP", 9, kDim, "center", true, 1.0);
				a_ui.Text(clip, "b", 2, padX - 30, padY - 1, 60, 18, step.name, 11, hover ? kWhite : kText, "center", true, 0.5);
				g_view.hits.push_back({ padX - 21, padY - 21, 42, 42, "step", []() {
					g_view.step = (g_view.step + 1) % kSteps.size();
					g_view.needsBuild = true;
				} });
			}

			// Turn / raise / tilt, beside the pad.
			const float bx = x + 160, bw = 66, bh = 26;
			float       by = padY - gap - 13;
			AddButton(a_ui, a_root, depth, "turnl", "TURN L", bx, by, bw, bh, []() { HandleEditKey(SelectMenu::EditKey::kTurnLeft); }, movable);
			AddButton(a_ui, a_root, depth, "turnr", "TURN R", bx + bw + 6, by, bw, bh, []() { HandleEditKey(SelectMenu::EditKey::kTurnRight); }, movable);
			by += bh + 8;
			AddButton(a_ui, a_root, depth, "raise", "RAISE", bx, by, bw, bh, []() { HandleEditKey(SelectMenu::EditKey::kRaise); }, movable);
			AddButton(a_ui, a_root, depth, "lower", "LOWER", bx + bw + 6, by, bw, bh, []() { HandleEditKey(SelectMenu::EditKey::kLower); }, movable);
			by += bh + 8;
			AddButton(a_ui, a_root, depth, "tiltu", "TILT UP", bx, by, bw, bh, []() { HandleEditKey(SelectMenu::EditKey::kTiltUp); }, camera);
			AddButton(a_ui, a_root, depth, "tiltd", "TILT DN", bx + bw + 6, by, bw, bh, []() { HandleEditKey(SelectMenu::EditKey::kTiltDown); }, camera);

			// Save / cancel / reset.
			float ay = padY + gap + 56;
			AddButton(a_ui, a_root, depth, "save", "SAVE & CLOSE", x, ay, w, 30, []() { EditSaveClose(); }, true, true);
			ay += 38;
			AddButton(a_ui, a_root, depth, "cancel", "CANCEL", x, ay, (w - 8) / 2, 28, []() { EditCancel(); });
			AddButton(a_ui, a_root, depth, "reset", g_view.resetArmed ? "CLICK AGAIN" : "RESET TO DEFAULT", x + (w + 8) / 2, ay, (w - 8) / 2, 28, []() {
				if (g_view.resetArmed) {
					g_view.resetArmed = false;
					QueueTask([]() { lobby::EditResetCell(); });
				} else {
					g_view.resetArmed = true;
				}
				g_view.needsBuild = true;
			});

			// Keys, bottom-left (the panel takes the right).
			const float left = a_ui.Visible().left + 52.0f;
			if (g_view.gamepad) {
				BuildLegend(a_ui, a_root, "editLegend", depth++, left, 688.0f, { { { "dpad" }, "Move" }, { { "pad:A" }, "Save" }, { { "pad:B" }, "Cancel" } });
			} else {
				BuildLegend(a_ui, a_root, "editLegend1", depth++, left, 662.0f,
					{ { { "W", "A", "S", "D" }, "Move" }, { { "Q", "E" }, "Turn" }, { { "R", "F" }, "Raise / lower" }, { { "T", "G" }, "Tilt" } });
				BuildLegend(a_ui, a_root, "editLegend2", depth++, left, 688.0f,
					{ { { "Tab" }, "Next" }, { { "1-9" }, "Character" }, { { "0" }, "Camera" }, { { "Enter" }, "Save" }, { { "Esc" }, "Cancel" } });
			}
		}

		void BuildSelect(Ui& a_ui, RE::GFxValue& a_root)
		{
			int depth = 1;

			// Soft shading at the top and bottom so text reads over the scene.
			auto shade = a_ui.Clip(a_root, "shade", depth++);
			Fade(a_ui, shade, 0, 0, kStageW, 90, 60.0, 0.0);
			a_ui.Gradient(shade, 0, 430, kStageW, 290, kBlack, { { 0.0, 0.0 }, { 55.0, 110.0 }, { 88.0, 255.0 } }, true);

			BuildPlates(a_ui, a_root, depth, g_view.selected);

			// Main menu actions, bottom-right and right-aligned like Skyrim's own
			// menus, as plain entries; the button legend goes bottom-left. Both
			// keep to the screen's edges on wider screens.
			const auto vis = a_ui.Visible();
			float      y = 582;
			for (std::size_t i = 0; i < kMenuItems.size(); ++i) {
				const auto& item = kMenuItems[i];
				AddLink(a_ui, a_root, depth, item.id, item.label, vis.right - 52.0f - 220.0f, y, 220, 14, "right", item.action, g_view.menuFocus == i);
				y += 26;
			}

			// The selected character, bottom centre: name, a rule, details, and ENTER.
			if (g_view.selected < g_view.slots.size()) {
				const auto& sl = g_view.slots[g_view.selected];
				const float cx = kStageW / 2.0f;
				const bool  hover = g_view.hover == "play";
				auto        hero = a_ui.Clip(a_root, "selected", depth++);
				a_ui.Text(hero, "name", 1, cx - 300, 548, 600, 44, sl.name, 32, kWhite, "center", true, 2.0);
				a_ui.Rule(hero, cx, 594, 460, 55.0);
				a_ui.Shadow(hero);
				a_ui.Text(hero, "info", 2, cx - 320, 600, 640, 22,
					std::format("Level {} {}{}{}{}{}", sl.level, sl.race, kDot, sl.location, kDot, charselect::ui::FriendlyDate(sl.lastPlayed)), 13, kText, "center");
				if (sl.placeholder) {
					// Over the scene, above the name: on a soft dark band, as a
					// bright background (the Bannered Mare's hearth) washed it out.
					auto note = a_ui.Clip(a_root, "note", depth++);
					a_ui.Gradient(note, cx - 280, 516, 560, 28, kBlack, { { 0.0, 0.0 }, { 70.0, 60.0 }, { 70.0, 195.0 }, { 0.0, 255.0 } }, false);
					a_ui.Text(note, "text", 1, cx - 320, 519, 640, 22, "Appearance not saved yet. Load this character once to update it.", 13, kText,
						"center");
					a_ui.Shadow(note);
				}

				// ENTER (loads their latest save): large spaced capitals between two fading rules; glows on hover.
				auto        play = a_ui.Clip(a_root, "play", depth++);
				const float py = 630;
				const float ruleW = hover ? 110.0f : 80.0f;
				a_ui.Text(play, "label", 1, cx - 150, py, 300, 36, sl.ready || sl.placeholder || sl.failed ? "ENTER" : "LOADING", 24, hover ? kWhite : kText,
					"center", true, 6.0);
				a_ui.Rule(play, cx - 70 - ruleW / 2.0f, py + 18, ruleW, hover ? 90.0 : 45.0);
				a_ui.Rule(play, cx + 70 + ruleW / 2.0f, py + 18, ruleW, hover ? 90.0 : 45.0);
				a_ui.Glow(play, kWhite, hover ? 0.6 : 0.25, hover ? 10.0 : 6.0, hover ? 1.6 : 1.0);
				g_view.hits.push_back({ cx - 190, py - 4, 380, 44, "play", []() { DoPlay(); } });
			}

			std::vector<LegendItem> legend;
			if (g_view.gamepad) {
				legend = g_view.menuFocus ? std::vector<LegendItem>{ { { "dpad" }, "Choose" }, { { "pad:A" }, "Select" }, { { "pad:B" }, "Back" } } :
				                            std::vector<LegendItem>{ { { "dpad" }, "Choose" }, { { "pad:A" }, "Enter" }, { { "pad:X" }, "Load" } };
			} else {
				legend = g_view.menuFocus ? std::vector<LegendItem>{ { { "^", "v" }, "Choose" }, { { "Enter" }, "Select" } } :
				                            std::vector<LegendItem>{ { { "<", ">" }, "Choose" }, { { "v" }, "Menu" }, { { "Enter" }, "Enter" },
												{ { "S" }, "Load" }, { { "F8" }, "Layout" } };
			}
			BuildLegend(a_ui, a_root, "legend", depth++, vis.left + 52.0f, 688.0f, legend);
		}

		void Build(RE::GFxMovieView* a_movie)
		{
			RE::GFxValue stage;
			a_movie->GetVariable(&stage, "_root");
			if (!stage.IsObject()) {
				logger::error("SelectMenu: no _root");
				return;
			}
			Ui ui(a_movie);
			// Recreating at the same depth replaces the previous build.
			auto root = ui.Clip(stage, "ui", 10);
			g_view.hits.clear();
			g_view.spinnerBuilt = false;
			if (g_view.mode == Mode::kSplash) {
				g_view.plateCount = 0;
				BuildSplash(ui, root);
			} else if (g_view.mode == Mode::kEdit) {
				BuildEdit(ui, root);
			} else {
				BuildSelect(ui, root);
			}
			g_view.needsBuild = false;
		}

		// ---- per-frame -------------------------------------------------------

		// A black layer over everything that fades out when the splash gives
		// way to the select screen. Its own clip above "ui", so rebuilds
		// underneath don't reset it.
		void UpdateFade(RE::GFxMovieView* a_movie, float a_interval)
		{
			RE::GFxValue stage;
			a_movie->GetVariable(&stage, "_root");
			if (!stage.IsObject()) {
				return;
			}
			RE::GFxValue fade;
			stage.GetMember("fade", &fade);
			if (g_view.fade <= 0.0f) {
				if (fade.IsDisplayObject()) {
					fade.Invoke("removeMovieClip");
				}
				return;
			}
			if (!fade.IsDisplayObject()) {
				Ui         ui(a_movie);
				const auto vis = ui.Visible();
				fade = ui.Clip(stage, "fade", 20);
				ui.Rect(fade, vis.left, vis.top, vis.right - vis.left, vis.bottom - vis.top, kBlack, 100.0);
			}
			g_view.fade = (std::max)(0.0f, g_view.fade - a_interval);
			// Ease out: quick at first, gentle at the end.
			const float t = g_view.fade / kSplashFade;
			fade.SetMember("_alpha", RE::GFxValue(static_cast<double>(100.0f * t * t)));
		}

		// The fade to black before leaving: its own clip above everything.
		void UpdateFadeOut(RE::GFxMovieView* a_movie, float a_interval)
		{
			RE::GFxValue stage;
			a_movie->GetVariable(&stage, "_root");
			if (!stage.IsObject()) {
				return;
			}
			RE::GFxValue layer;
			stage.GetMember("fadeOut", &layer);
			if (!Leaving()) {
				if (layer.IsDisplayObject()) {
					layer.Invoke("removeMovieClip");
				}
				return;
			}
			if (!layer.IsDisplayObject()) {
				Ui         ui(a_movie);
				const auto vis = ui.Visible();
				layer = ui.Clip(stage, "fadeOut", 21);
				ui.Rect(layer, vis.left, vis.top, vis.right - vis.left, vis.bottom - vis.top, kBlack, 100.0);
			}
			if (g_view.blackout) {
				layer.SetMember("_alpha", RE::GFxValue(100.0));
				return;
			}
			g_view.fadeOut += a_interval;
			const float t = (std::min)(g_view.fadeOut / kFadeOut, 1.0f);
			layer.SetMember("_alpha", RE::GFxValue(static_cast<double>(100.0f * t * (2.0f - t))));  // ease out
			if (t >= 1.0f) {
				g_view.fadeOut = -1.0f;
				g_view.blackout = true;
				if (auto then = std::exchange(g_view.afterFade, {})) {
					then();
				}
			}
		}

		// Steps a tick at a time (12 a second, one turn a second), like a
		// clock-style loading spinner.
		void UpdateSpinner(RE::GFxMovieView* a_movie, float a_interval)
		{
			RE::GFxValue spinner;
			if (g_view.mode != Mode::kSplash || !g_view.spinnerBuilt || !a_movie->GetVariable(&spinner, "_root.ui.spinner") ||
				!spinner.IsDisplayObject()) {
				return;
			}
			g_view.spinTime = std::fmod(g_view.spinTime + a_interval, 1.0f);
			const double degrees = std::floor(g_view.spinTime * 12.0f) * 30.0;
			spinner.SetMember("_rotation", RE::GFxValue(degrees));
		}

		struct StageMapping
		{
			float visLeft{ 0 }, visTop{ 0 }, visW{ kStageW }, visH{ kStageH };
			float vpLeft{ 0 }, vpTop{ 0 }, vpW{ 1 }, vpH{ 1 };
		};

		StageMapping GetMapping(RE::GFxMovieView* a_movie)
		{
			StageMapping m;
			const auto   vis = a_movie->GetVisibleFrameRect();
			m.visLeft = vis.left;
			m.visTop = vis.top;
			m.visW = vis.right - vis.left;
			m.visH = vis.bottom - vis.top;
			RE::GViewport vp;
			a_movie->GetViewport(&vp);
			m.vpLeft = static_cast<float>(vp.left);
			m.vpTop = static_cast<float>(vp.top);
			m.vpW = static_cast<float>((std::max)(vp.width, 1));
			m.vpH = static_cast<float>((std::max)(vp.height, 1));
			return m;
		}

		// The game cursor (screen pixels) mapped into stage coordinates. GFx's
		// own _root._xmouse never updates in this menu.
		void UpdateCursor(const StageMapping& a_map)
		{
			float cx = -1.0f, cy = -1.0f;
			if (auto* cursor = RE::MenuCursor::GetSingleton()) {
				const auto& rd = cursor->GetRuntimeData();
				cx = a_map.visLeft + (rd.cursorPosX - a_map.vpLeft) / a_map.vpW * a_map.visW;
				cy = a_map.visTop + (rd.cursorPosY - a_map.vpTop) / a_map.vpH * a_map.visH;
			}
			g_view.cursorX = cx;
			g_view.cursorY = cy;
		}

		bool Project(const RE::NiPoint3& a_world, const StageMapping& a_map, float& a_x, float& a_y)
		{
			auto* camera = RE::Main::WorldRootCamera();
			if (!camera) {
				return false;
			}
			float x = 0, y = 0, z = 0;
			camera->WorldPtToScreenPt3(a_world, x, y, z, 1e-5f);
			if (z < 0.0f || x < -0.2f || x > 1.2f || y < -0.2f || y > 1.2f) {
				return false;
			}
			a_x = a_map.visLeft + x * a_map.visW;
			a_y = a_map.visTop + (1.0f - y) * a_map.visH;
			return true;
		}

		// Plates are looked up by path each frame rather than kept: a handle kept
		// past a frame is released in the menu's destructor after its movie has
		// gone (a crash after a cancelled LOAD).
		void UpdatePlates(RE::GFxMovieView* a_movie, const StageMapping& a_map)
		{
			g_view.actorHits.clear();
			for (std::size_t i = 0; i < g_view.slots.size() && i < g_view.plateCount; ++i) {
				RE::GFxValue plate;
				if (!a_movie->GetVariable(&plate, std::format("_root.ui.plates.plate{}", i).c_str()) || !plate.IsDisplayObject()) {
					continue;
				}
				const auto&                 slot = g_view.slots[i];
				auto                        ref = slot.ref.get();
				bool                        visible = false;
				std::optional<RE::NiPoint3> where;
				if (ref && slot.ready && !ref->IsDisabled()) {
					where = ref->GetPosition();
				} else if (slot.placeholder || slot.failed) {
					where = slot.pos;  // the character couldn't be shown; the plate marks the spot
				}
				if (where) {
					const auto feet = *where;
					auto       head = feet;
					head.z += 150.0f;
					float hx, hy, fx, fy;
					if (Project(head, a_map, hx, hy) && Project(feet, a_map, fx, fy)) {
						visible = true;
						plate.SetMember("_x", RE::GFxValue(static_cast<double>(hx)));
						plate.SetMember("_y", RE::GFxValue(static_cast<double>(hy - 50.0f)));
						const float height = (std::max)(fy - hy, 20.0f);
						const float width = height * 0.45f;
						g_view.actorHits.push_back({ hx - width / 2.0f, hy, width, height, std::format("actor{}", i), [i]() {
							if (g_view.mode == Mode::kEdit) {
								SetEditTarget(i + 1);
							} else {
								Select(i);
							}
						} });
					}
				}
				plate.SetMember("_visible", RE::GFxValue(visible));
			}
		}

		const Hit* HitAt(float a_x, float a_y)
		{
			for (auto it = g_view.hits.rbegin(); it != g_view.hits.rend(); ++it) {
				if (it->Contains(a_x, a_y)) {
					return &*it;
				}
			}
			for (const auto& h : g_view.actorHits) {
				if (h.Contains(a_x, a_y)) {
					return &h;
				}
			}
			return nullptr;
		}

		void UpdateHover()
		{
			const auto* hit = HitAt(g_view.cursorX, g_view.cursorY);
			std::string id = hit ? hit->id : std::string{};
			// Panel backgrounds aren't hoverable.
			if (id == "editPanel") {
				id.clear();
			}
			if (id != g_view.hover) {
				if (!id.empty()) {
					Sound("UIMenuFocus");
				}
				g_view.hover = std::move(id);
				g_view.needsBuild = true;
			}
		}
	}

	// ---- statics -------------------------------------------------------------

	void SelectMenu::Register()
	{
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->Register(MENU_NAME, []() -> RE::IMenu* { return new SelectMenu(); });
			logger::debug("SelectMenu: registered '{}'", MENU_NAME);
		}
	}

	void SelectMenu::ShowSplash(std::string_view a_text)
	{
		{
			std::scoped_lock lock(g_shared.lock);
			g_shared.mode = Mode::kSplash;
			g_shared.reshow = true;
			g_shared.splashText = std::string(a_text);
			g_shared.dirty = true;
		}
		if (!IsOpen()) {
			RE::UIMessageQueue::GetSingleton()->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
		}
		logger::debug("SelectMenu: splash '{}'", a_text);
	}

	void SelectMenu::SetGamepad(bool a_gamepad)
	{
		std::scoped_lock lock(g_shared.lock);
		if (g_shared.gamepad != a_gamepad) {
			g_shared.gamepad = a_gamepad;
			g_shared.dirty = true;
		}
	}

	void SelectMenu::SetSplashText(std::string_view a_text)
	{
		std::scoped_lock lock(g_shared.lock);
		if (g_shared.mode == Mode::kSplash && g_shared.splashText != a_text) {
			g_shared.splashText = std::string(a_text);
			g_shared.dirty = true;
		}
	}

	void SelectMenu::ShowSelect()
	{
		auto slots = lobby::GetSlots();
		// A new menu starts on the first character, but a lobby resumed after
		// a cancelled load still has the light on the one that was picked.
		const auto lit = lobby::HighlightedSlot();
		{
			std::scoped_lock lock(g_shared.lock);
			g_shared.mode = Mode::kSelect;
			g_shared.reshow = true;
			g_shared.slots = std::move(slots);
			g_shared.highlighted = lit;
			g_shared.dirty = true;
		}
		if (!IsOpen()) {
			RE::UIMessageQueue::GetSingleton()->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
		}
		logger::debug("SelectMenu: select screen");
	}

	void SelectMenu::ShowEdit()
	{
		auto slots = lobby::GetSlots();
		{
			std::scoped_lock lock(g_shared.lock);
			g_shared.mode = Mode::kEdit;
			g_shared.reshow = true;
			g_shared.slots = std::move(slots);
			g_shared.dirty = true;
		}
		if (!IsOpen()) {
			RE::UIMessageQueue::GetSingleton()->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, nullptr);
		}
		logger::debug("SelectMenu: layout editor");
	}

	void SelectMenu::QueueEdit(EditKey a_key)
	{
		std::scoped_lock lock(g_shared.lock);
		g_shared.editKeys.push_back(a_key);
	}

	void SelectMenu::Hide()
	{
		// Queued even when the menu isn't open yet: a kShow may still be in the
		// queue, and the hide must follow it. A hide for a menu
		// that isn't open is ignored by the UI.
		if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
			queue->AddMessage(MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
			logger::debug("SelectMenu: hide");
		}
	}

	bool SelectMenu::MovieFailed()
	{
		return g_movieFailed;
	}

	bool SelectMenu::IsFading()
	{
		return g_fading;
	}

	bool SelectMenu::IsOpen()
	{
		auto* ui = RE::UI::GetSingleton();
		return ui && ui->IsMenuOpen(MENU_NAME);
	}

	void SelectMenu::QueueClick(bool a_repeat)
	{
		std::scoped_lock lock(g_shared.lock);
		++(a_repeat ? g_shared.repeatClicks : g_shared.clicks);
	}

	void SelectMenu::QueueNav(Nav a_nav)
	{
		std::scoped_lock lock(g_shared.lock);
		g_shared.navs.push_back(a_nav);
	}

	void SelectMenu::QueueSelect(std::size_t a_slot)
	{
		std::scoped_lock lock(g_shared.lock);
		g_shared.selectRequest = a_slot;
	}

	// ---- instance ------------------------------------------------------------

	SelectMenu::SelectMenu()
	{
		using Flag = RE::UI_MENU_FLAGS;
		depthPriority = 4;
		menuFlags.set(Flag::kUsesCursor, Flag::kDisablePauseMenu, Flag::kUsesMenuContext, Flag::kRequiresUpdate);
		inputContext = Context::kMenuMode;

		auto* scaleform = RE::BSScaleformManager::GetSingleton();
		const bool loaded = scaleform && scaleform->LoadMovie(this, uiMovie, FILE_NAME.data(), RE::GFxMovieView::ScaleModeType::kShowAll, 0.0f);
		logger::info("SelectMenu: created, movie {} ({})", loaded && uiMovie ? "loaded" : "FAILED to load", FILE_NAME);
		if (!loaded || !uiMovie) {
			// No UI at all: the player would be stuck behind a lobby they can't
			// see or leave. The frontend falls back to the main menu.
			g_movieFailed = true;
			QueueTask([]() { frontend::OnSelectMenuUnavailable(); });
		}

		g_view = View{};
		g_fading = false;
		std::scoped_lock lock(g_shared.lock);
		g_shared.dirty = true;
	}

	SelectMenu::~SelectMenu()
	{
		logger::debug("SelectMenu: destroyed");
	}

	RE::UI_MESSAGE_RESULTS SelectMenu::ProcessMessage(RE::UIMessage& a_message)
	{
		auto result = RE::UI_MESSAGE_RESULTS::kPassOn;
		Guarded("SelectMenu message", [&]() { result = HandleMessage(a_message); });
		return result;
	}

	RE::UI_MESSAGE_RESULTS SelectMenu::HandleMessage(RE::UIMessage& a_message)
	{
		switch (*a_message.type) {
		case RE::UI_MESSAGE_TYPE::kShow:
			logger::debug("SelectMenu: kShow");
			break;
		case RE::UI_MESSAGE_TYPE::kHide:
			logger::debug("SelectMenu: kHide");
			break;
		default:
			break;
		}
		return RE::IMenu::ProcessMessage(a_message);
	}

	void SelectMenu::AdvanceMovie(float a_interval, std::uint32_t a_currentTime)
	{
		Guarded("SelectMenu frame", [&]() { Advance(a_interval, a_currentTime); });
	}

	void SelectMenu::Advance(float a_interval, std::uint32_t a_currentTime)
	{
		RE::IMenu::AdvanceMovie(a_interval, a_currentTime);
		if (!uiMovie) {
			return;
		}

		int                              clicks = 0;
		int                              repeatClicks = 0;
		std::vector<Nav>                 navs;
		std::optional<std::size_t>       selectRequest;
		std::vector<EditKey>             editKeys;
		{
			std::scoped_lock lock(g_shared.lock);
			if (g_shared.dirty) {
				if (g_view.mode == Mode::kSplash && g_shared.mode == Mode::kSelect) {
					g_view.fade = kSplashFade;
				}
				if (std::exchange(g_shared.reshow, false)) {
					// A new screen (e.g. back to the select screen after a cancelled load).
					g_view.fadeOut = -1.0f;
					g_view.blackout = false;
					g_view.afterFade = {};
					g_fading = false;
				}
				g_view.mode = g_shared.mode;
				if (g_view.mode == Mode::kEdit) {
					g_view.resetArmed = false;
				}
				g_view.splashText = g_shared.splashText;
				g_view.gamepad = g_shared.gamepad;
				g_view.slots = g_shared.slots;
				if (const auto lit = std::exchange(g_shared.highlighted, std::nullopt)) {
					g_view.selected = *lit;
				}
				if (g_view.selected >= g_view.slots.size()) {
					g_view.selected = 0;
				}
				g_view.order.resize(g_view.slots.size());
				std::iota(g_view.order.begin(), g_view.order.end(), std::size_t{ 0 });
				std::ranges::stable_sort(g_view.order, [](std::size_t a, std::size_t b) {
					return g_view.slots[a].lateral < g_view.slots[b].lateral;
				});
				g_view.needsBuild = true;
				g_shared.dirty = false;
			}
			clicks = std::exchange(g_shared.clicks, 0);
			repeatClicks = std::exchange(g_shared.repeatClicks, 0);
			navs = std::exchange(g_shared.navs, {});
			selectRequest = std::exchange(g_shared.selectRequest, std::nullopt);
			editKeys = std::exchange(g_shared.editKeys, {});
		}

		// The editor's targets change as things move; rebuild when they do.
		if (g_view.mode == Mode::kEdit) {
			auto fresh = lobby::GetEditTargets();
			if (fresh != g_view.editTargets) {
				g_view.editTargets = std::move(fresh);
				g_view.needsBuild = true;
			}
		}

		// Readiness changes as slots finish; refresh it so plates appear.
		if (g_view.mode == Mode::kSelect || g_view.mode == Mode::kEdit) {
			const auto fresh = lobby::GetSlotStatus();
			if (fresh.size() == g_view.slots.size()) {
				for (std::size_t i = 0; i < fresh.size(); ++i) {
					auto& slot = g_view.slots[i];
					if (fresh[i].ready != slot.ready || fresh[i].failed != slot.failed) {
						slot.ready = fresh[i].ready;
						slot.failed = fresh[i].failed;
						slot.ref = fresh[i].ref;
						g_view.needsBuild = true;
					}
				}
			}
		}

		const auto map = GetMapping(uiMovie.get());
		const bool interactive = (g_view.mode == Mode::kSelect || g_view.mode == Mode::kEdit) && !Leaving();
		if (interactive) {
			UpdateCursor(map);
			if (g_view.mode == Mode::kEdit) {
				if (selectRequest) {
					SetEditTarget(*selectRequest + 1);
				}
				for (const auto key : editKeys) {
					HandleEditKey(key);
				}
			} else {
				if (selectRequest) {
					Select(*selectRequest);
				}
				for (const auto nav : navs) {
					HandleNav(nav);
				}
			}
			for (int i = 0; i < clicks; ++i) {
				if (const auto* hit = HitAt(g_view.cursorX, g_view.cursorY)) {
					logger::debug("SelectMenu: click '{}' at ({:.0f}, {:.0f})", hit->id, g_view.cursorX, g_view.cursorY);
					const bool cycle = hit->id.ends_with("_prev") || hit->id.ends_with("_next");
					if (hit->id != "editPanel") {  // the panel's background
						Sound(cycle ? "UIMenuPrevNext" : hit->id == "cancel" ? "UIMenuCancel" : "UIMenuOK");
					}
					auto action = hit->action;
					action();
				} else {
					logger::debug("SelectMenu: click on nothing at ({:.0f}, {:.0f})", g_view.cursorX, g_view.cursorY);
				}
			}
			// Holding the mouse on a move/turn/raise/tilt control repeats it; nothing else repeats.
			static constexpr std::array<std::string_view, 10> kRepeatable{ "fwd", "back", "left", "right", "turnl", "turnr", "raise", "lower", "tiltu", "tiltd" };
			for (int i = 0; i < repeatClicks && g_view.mode == Mode::kEdit; ++i) {
				if (const auto* hit = HitAt(g_view.cursorX, g_view.cursorY); hit && std::ranges::find(kRepeatable, hit->id) != kRepeatable.end()) {
					auto action = hit->action;
					action();
				}
			}
		}

		if (g_view.needsBuild) {
			Build(uiMovie.get());
		}
		UpdateFade(uiMovie.get(), a_interval);
		UpdateFadeOut(uiMovie.get(), a_interval);
		UpdateSpinner(uiMovie.get(), a_interval);
		if (interactive) {
			UpdatePlates(uiMovie.get(), map);
			UpdateHover();
			if (g_view.needsBuild) {
				Build(uiMovie.get());
				UpdatePlates(uiMovie.get(), map);
			}
		}
	}
}
