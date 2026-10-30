// Makes lobby characters stand, sit, sleep or play an idle, and keeps them
// there (see Poses.h).

#include "Poses.h"

#include "Util.h"

namespace charselect::poses
{
	namespace
	{
		// Skyrim.esm forms
		constexpr RE::FormID kSitLinkedRefPackage = 0x0ECEEB;  // defaultSitLinkedRefNoConv: sit in the linked ref
		// ambushSleepPackage: sleep in the linked ref itself, any time; its one
		// condition is Variable01 == 0 (ambushers wake when it's set). Not
		// pelagiusHoldPosSleepIgnoreCombat, which first searches for a bed near
		// the linked ref: too slow for the instant pop, so the sleeper walks.
		constexpr RE::FormID kSleepLinkedRefPackage = 0x09448B;
		constexpr RE::FormID kEnterChairInstant = 0x031728;    // idle, event IdleChairEnterInstant

		constexpr auto  kPollInterval = std::chrono::milliseconds(250);
		constexpr int   kSitActivatePoll = 12;    // 3 s not trying to sit -> activate the furniture directly
		constexpr int   kSitIdlePoll = 24;        // 6 s -> play the instant-enter idle on it
		constexpr int   kSitGiveUpPoll = 48;      // 12 s
		constexpr int   kStandQuickPoll = 6;      // 1.5 s still seated after the exit event -> StopInteractingQuick
		constexpr int   kStandGiveUpPoll = 24;    // 6 s
		constexpr auto  kSettleDelay = std::chrono::milliseconds(1000);  // let a sit/stand animation finish before an idle
		constexpr auto  kEventRetryDelay = std::chrono::milliseconds(1500);
		constexpr int   kEventTries = 3;
		constexpr auto  kWatchInterval = std::chrono::seconds(2);
		constexpr float kDriftLimit = 60.0f;

		struct Performer
		{
			RE::ObjectRefHandle actor;
			RE::ObjectRefHandle furniture;
			RE::NiPoint3        home;
			float               heading{ 0.0f };
			Pose                rest;
			std::optional<Pose> select;
			Pose                current;
			bool                started{ false };
			bool                busy{ false };  // sitting down or standing up
			std::uint32_t       token{ 0 };     // bumped by every Apply; stale steps stop
			bool                warnedNoSeat{ false };  // a seated pose without a seat, logged once
		};

		std::vector<Performer>     g_performers;
		std::optional<std::size_t> g_selected;
		std::uint32_t              g_epoch{ 0 };  // bumped by Reset
		bool                       g_watching{ false };
		RE::TESPackage*            g_doNothing{ nullptr };  // created once per lobby game (see Reset)

		// ---- packages --------------------------------------------------------

		RE::TESPackage* DoNothingPackage()
		{
			if (!g_doNothing) {
				g_doNothing = RE::TESPackage::CreatePackage(RE::PACKAGE_TYPE::kDoNothing);
				if (g_doNothing) {
					// No conversations, greetings or idle chatter: with these on
					// (vanilla DoNothing has them all) the characters talk and
					// turn to face each other. defaultSitLinkedRefNoConv has
					// the same set off.
					using Flag = RE::PACKAGE_DATA::InterruptFlag;
					g_doNothing->packData.foBehaviorFlags.reset(Flag::kHellosToPlayer, Flag::kRandomConversations, Flag::kAllowIdleChatter,
						Flag::kWorldInteractions, Flag::kReactionToPlayerActions, Flag::kGreetCorpseBehaviour, Flag::kFriendlyFireComments);
				}
				logger::debug("Poses: created do-nothing package {:08X} (behavior flags {:04X})", g_doNothing ? g_doNothing->GetFormID() : 0,
					g_doNothing ? g_doNothing->packData.foBehaviorFlags.underlying() : 0);
			}
			return g_doNothing;
		}

		RE::TESPackage* SitPackage()
		{
			static RE::TESPackage* package = nullptr;
			if (!package) {
				package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(kSitLinkedRefPackage, "Skyrim.esm");
				if (!package) {
					logger::error("Poses: Skyrim.esm defaultSitLinkedRefNoConv ({:06X}) not found; nobody can sit", kSitLinkedRefPackage);
				}
			}
			return package;
		}

		RE::TESPackage* SleepPackage()
		{
			static RE::TESPackage* package = nullptr;
			if (!package) {
				package = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESPackage>(kSleepLinkedRefPackage, "Skyrim.esm");
				if (!package) {
					logger::error("Poses: Skyrim.esm sleep package ({:06X}) not found; nobody can sleep", kSleepLinkedRefPackage);
				}
			}
			return package;
		}

		RE::TESPackage* FurniturePackage(const Pose& a_pose)
		{
			return a_pose.sleep ? SleepPackage() : SitPackage();
		}

		bool IsOurPackage(const RE::TESPackage* a_package)
		{
			return a_package && (a_package == g_doNothing || a_package == SitPackage() || a_package == SleepPackage());
		}

		// Puts a_package on top of the base's package list (the base is the
		// slot's own runtime copy). Returns true if it changed.
		bool SetTopPackage(RE::TESNPC* a_npc, RE::TESPackage* a_package)
		{
			if (!a_npc || !a_package) {
				return false;
			}
			auto& list = a_npc->aiPackages.packages;
			if (!list.empty() && IsOurPackage(list.front())) {
				if (list.front() == a_package) {
					return false;
				}
				list.front() = a_package;  // our own entry: swap it
			} else {
				list.push_front(a_package);  // never touch entries copied from the player's base
			}
			return true;
		}

		// ---- performers ------------------------------------------------------

		Performer* Get(std::size_t a_index)
		{
			return a_index < g_performers.size() ? &g_performers[a_index] : nullptr;
		}

		// The performer, if a delayed step for it is still current.
		Performer* GetLive(std::uint32_t a_epoch, std::size_t a_index, std::uint32_t a_token)
		{
			auto* p = a_epoch == g_epoch ? Get(a_index) : nullptr;
			return p && p->token == a_token ? p : nullptr;
		}

		RE::Actor* ActorOf(const Performer& a_p)
		{
			auto ref = a_p.actor.get();
			return ref ? ref->As<RE::Actor>() : nullptr;
		}

		RE::SIT_SLEEP_STATE SitState(RE::Actor* a_actor)
		{
			auto* state = a_actor ? a_actor->AsActorState() : nullptr;
			return state ? state->GetSitSleepState() : RE::SIT_SLEEP_STATE::kNormal;
		}

		bool Seated(RE::Actor* a_actor)
		{
			return SitState(a_actor) == RE::SIT_SLEEP_STATE::kIsSitting;
		}

		// A package that isn't one of ours is running (a created one the
		// engine or an old fallback left behind); package evaluation won't
		// replace it, an AI reset does.
		bool RunningForeignPackage(RE::Actor* a_actor)
		{
			auto* process = a_actor->GetActorRuntimeData().currentProcess;
			auto* package = process ? process->GetRunningPackage() : nullptr;
			return package && !IsOurPackage(package);
		}

		// Everyone looks at the camera (the player) rather than at each other.
		void LookAtCamera(RE::Actor* a_actor)
		{
			auto* process = a_actor->GetActorRuntimeData().currentProcess;
			auto* high = process ? process->high : nullptr;
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (high && player) {
				high->SetHeadtrackTarget(RE::HighProcessData::HEAD_TRACK_TYPE::kScript, player);
			}
		}

		std::string HeadTarget(RE::Actor* a_actor)
		{
			auto* process = a_actor->GetActorRuntimeData().currentProcess;
			auto  target = process ? process->GetHeadtrackTarget().get() : nullptr;
			if (!target) {
				return "none";
			}
			const char* name = target->GetDisplayFullName();
			return std::format("{:08X} '{}'", target->GetFormID(), name ? name : "");
		}

		// Sitting (or, for a sleep pose, lying) in its own furniture.
		bool InPlace(RE::Actor* a_actor, const RE::ObjectRefHandle& a_furniture, const Pose& a_pose)
		{
			const auto wanted = a_pose.sleep ? RE::SIT_SLEEP_STATE::kIsSleeping : RE::SIT_SLEEP_STATE::kIsSitting;
			return SitState(a_actor) == wanted && a_furniture && a_actor->GetOccupiedFurniture() == a_furniture;
		}

		std::string Describe(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return "no actor";
			}
			auto*      process = a_actor->GetActorRuntimeData().currentProcess;
			auto*      package = process ? process->GetRunningPackage() : nullptr;
			const auto furniture = a_actor->GetOccupiedFurniture().get();
			auto*      state = a_actor->AsActorState();
			return std::format("sit state {} furniture {:08X} package {} AI={} weapon drawn={} looking at {}",
				static_cast<std::uint32_t>(SitState(a_actor)), furniture ? furniture->GetFormID() : 0,
				package ? std::format("{:08X} type {}", package->GetFormID(), static_cast<int>(package->packData.packType.underlying())) : "none",
				a_actor->IsAIEnabled(), state && state->IsWeaponDrawn(), HeadTarget(a_actor));
		}

		void SendPoseEvent(std::uint32_t a_epoch, std::size_t a_index, std::uint32_t a_token, int a_try)
		{
			auto* p = GetLive(a_epoch, a_index, a_token);
			auto* actor = p ? ActorOf(*p) : nullptr;
			if (!actor) {
				return;
			}
			const auto& event = p->current.event;
			const bool  ok = actor->NotifyAnimationGraph(RE::BSFixedString(event));
			logger::debug("Poses: slot {} graph event '{}' -> {} (try {})", a_index + 1, event, ok ? "accepted" : "rejected", a_try + 1);
			if (!ok && a_try + 1 < kEventTries) {
				RunAfter(kEventRetryDelay, [a_epoch, a_index, a_token, a_try]() { SendPoseEvent(a_epoch, a_index, a_token, a_try + 1); });
			}
		}

		// In position: draw the weapon and/or start the idle. a_moved: it just
		// sat down or stood up, so give that animation time to finish.
		void Finish(std::size_t a_index, bool a_moved)
		{
			auto* p = Get(a_index);
			auto* actor = p ? ActorOf(*p) : nullptr;
			if (!actor) {
				return;
			}
			p->busy = false;
			const auto& pose = p->current;
			if (pose.sit && !InPlace(actor, p->furniture, pose)) {
				logger::warn("Poses: slot {} '{}' is not seated in its furniture; skipping its idle ({})", a_index + 1, pose.name, Describe(actor));
				return;
			}
			if (pose.weaponDrawn) {
				actor->DrawWeaponMagicHands(true);
				logger::debug("Poses: slot {} drawing weapon", a_index + 1);
			}
			if (!pose.event.empty()) {
				const auto epoch = g_epoch;
				const auto token = p->token;
				if (a_moved) {
					RunAfter(kSettleDelay, [epoch, a_index, token]() { SendPoseEvent(epoch, a_index, token, 0); });
				} else {
					SendPoseEvent(epoch, a_index, token, 0);
				}
			}
			logger::debug("Poses: slot {} in pose '{}' ({})", a_index + 1, pose.name, Describe(actor));
		}

		void PollSit(std::uint32_t a_epoch, std::size_t a_index, std::uint32_t a_token, int a_poll)
		{
			auto* p = GetLive(a_epoch, a_index, a_token);
			auto* actor = p ? ActorOf(*p) : nullptr;
			if (!actor) {
				return;
			}
			if (InPlace(actor, p->furniture, p->current)) {
				logger::debug("Poses: slot {} {} after {} ms", a_index + 1, p->current.sleep ? "asleep" : "seated", a_poll * kPollInterval.count());
				Finish(a_index, true);
				return;
			}
			const auto state = SitState(actor);
			auto       furniture = p->furniture.get();
			if (a_poll == kSitActivatePoll && state == RE::SIT_SLEEP_STATE::kNormal && furniture) {
				logger::warn("Poses: slot {} not sitting under the sit package ({}); AI reset", a_index + 1, Describe(actor));
				actor->EvaluatePackage(true, true);
				const bool ok = furniture->ActivateRef(actor, 0, nullptr, 1, false);
				logger::debug("Poses: slot {} activating the furniture -> {}", a_index + 1, ok);
			}
			if (a_poll == kSitIdlePoll && state == RE::SIT_SLEEP_STATE::kNormal && furniture && !p->current.sleep) {
				auto*      idle = RE::TESDataHandler::GetSingleton()->LookupForm<RE::TESIdleForm>(kEnterChairInstant, "Skyrim.esm");
				auto*      process = actor->GetActorRuntimeData().currentProcess;
				const bool ok = idle && process && process->PlayIdle(actor, idle, furniture.get());
				logger::warn("Poses: slot {} still not sitting ({}); EnterChairInstant idle -> {}", a_index + 1, Describe(actor), ok);
			}
			if (a_poll >= kSitGiveUpPoll) {
				// Don't leave it stuck wherever it got to: back on its spot,
				// standing, facing the camera.
				logger::warn("Poses: slot {} never got into its furniture ({}); standing on its spot instead", a_index + 1, Describe(actor));
				p->current = Pose{ "stand" };
				p->busy = false;
				SetTopPackage(actor->GetActorBase(), DoNothingPackage());
				actor->EvaluatePackage(true, true);
				actor->SetPosition(p->home, true);
				actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, p->heading });
				LookAtCamera(actor);
				return;
			}
			RunAfter(kPollInterval, [a_epoch, a_index, a_token, a_poll]() { PollSit(a_epoch, a_index, a_token, a_poll + 1); });
		}

		void StartSit(std::size_t a_index, RE::Actor* a_actor)
		{
			auto& p = g_performers[a_index];
			p.busy = true;
			const auto state = SitState(a_actor);
			if ((state == RE::SIT_SLEEP_STATE::kWantToSit || state == RE::SIT_SLEEP_STATE::kWaitingForSitAnim ||
					state == RE::SIT_SLEEP_STATE::kWantToSleep || state == RE::SIT_SLEEP_STATE::kWaitingForSleepAnim) &&
				a_actor->GetOccupiedFurniture() == p.furniture) {
				// Already on its way into this furniture (AI has been on since it was enabled).
				logger::debug("Poses: slot {} already sitting down ({})", a_index + 1, Describe(a_actor));
				const auto epoch = g_epoch;
				const auto token = p.token;
				RunAfter(kPollInterval, [epoch, a_index, token]() { PollSit(epoch, a_index, token, 1); });
				return;
			}
			// Linked again in case the furniture changed (layout editor).
			a_actor->extraList.SetLinkedRef(p.furniture.get().get(), nullptr);
			SetTopPackage(a_actor->GetActorBase(), FurniturePackage(p.current));
			// What lets NPCs appear already seated when a cell loads, instead of walking over.
			a_actor->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kAllowInstantFurniturePopInPlayerCell);
			// With an AI reset: after the engine has taken an actor out of its
			// seat, package evaluation alone does nothing for seconds.
			a_actor->EvaluatePackage(true, true);
			logger::debug("Poses: slot {} sitting down ({})", a_index + 1, Describe(a_actor));
			const auto epoch = g_epoch;
			const auto token = p.token;
			RunAfter(kPollInterval, [epoch, a_index, token]() { PollSit(epoch, a_index, token, 1); });
		}

		void PollStand(std::uint32_t a_epoch, std::size_t a_index, std::uint32_t a_token, int a_poll)
		{
			auto* p = GetLive(a_epoch, a_index, a_token);
			auto* actor = p ? ActorOf(*p) : nullptr;
			if (!actor) {
				return;
			}
			const auto state = SitState(actor);
			if (state == RE::SIT_SLEEP_STATE::kNormal) {
				logger::debug("Poses: slot {} standing after {} ms", a_index + 1, a_poll * kPollInterval.count());
				// Make sure the do-nothing package is what runs now.
				actor->EvaluatePackage(true, RunningForeignPackage(actor));
				Finish(a_index, true);
				return;
			}
			if (a_poll == kStandQuickPoll && state == RE::SIT_SLEEP_STATE::kIsSitting) {
				logger::warn("Poses: slot {} still seated after the exit event ({}); StopInteractingQuick", a_index + 1, Describe(actor));
				actor->StopInteractingQuick(false);
			}
			if (a_poll >= kStandGiveUpPoll) {
				logger::warn("Poses: slot {} never stood up ({})", a_index + 1, Describe(actor));
				Finish(a_index, false);
				return;
			}
			RunAfter(kPollInterval, [a_epoch, a_index, a_token, a_poll]() { PollStand(a_epoch, a_index, a_token, a_poll + 1); });
		}

		void StartStand(std::size_t a_index, RE::Actor* a_actor)
		{
			auto& p = g_performers[a_index];
			p.busy = true;
			// Off the sit package first, or it sits straight back down. Package
			// changes alone don't get a seated actor up; the chair exit
			// animation does.
			SetTopPackage(a_actor->GetActorBase(), DoNothingPackage());
			const bool exitOk = a_actor->NotifyAnimationGraph("IdleChairExitStart");
			logger::debug("Poses: slot {} standing up, exit event {} ({})", a_index + 1, exitOk ? "accepted" : "rejected", Describe(a_actor));
			const auto epoch = g_epoch;
			const auto token = p.token;
			RunAfter(kPollInterval, [epoch, a_index, token]() { PollStand(epoch, a_index, token, 1); });
		}

		void Apply(std::size_t a_index, const Pose& a_pose, std::string_view a_why)
		{
			auto* p = Get(a_index);
			auto* actor = p ? ActorOf(*p) : nullptr;
			if (!p || !p->started || !actor) {
				return;
			}
			++p->token;
			const Pose previous = std::exchange(p->current, a_pose);
			const bool inFurniture = SitState(actor) != RE::SIT_SLEEP_STATE::kNormal;
			const bool wantSit = a_pose.sit && p->furniture.get() && FurniturePackage(a_pose);
			logger::debug("Poses: slot {} '{}' -> '{}' ({})", a_index + 1, previous.name, a_pose.name, a_why);
			if (a_pose.sit && !wantSit && !std::exchange(p->warnedNoSeat, true)) {
				logger::warn("Poses: slot {} pose '{}' needs furniture (slot{}furniture); standing instead", a_index + 1, a_pose.name, a_index + 1);
			}

			if (previous.weaponDrawn && !a_pose.weaponDrawn) {
				actor->DrawWeaponMagicHands(false);
			}
			// End a standing idle loop; seated ones end with getting up.
			if (!inFurniture && !previous.sit && !previous.event.empty() && previous.event != a_pose.event) {
				const bool ok = actor->NotifyAnimationGraph("IdleStop");
				logger::debug("Poses: slot {} graph event 'IdleStop' -> {}", a_index + 1, ok ? "accepted" : "rejected");
			}

			if (wantSit) {
				if (InPlace(actor, p->furniture, a_pose)) {
					Finish(a_index, false);
				} else {
					StartSit(a_index, actor);  // also gets it out of any other furniture
				}
			} else if (inFurniture) {
				StartStand(a_index, actor);
			} else {
				if (SetTopPackage(actor->GetActorBase(), DoNothingPackage())) {
					actor->EvaluatePackage(true, false);
				}
				Finish(a_index, false);
			}
		}

		const Pose& Wanted(std::size_t a_index)
		{
			const auto& p = g_performers[a_index];
			return g_selected == a_index && p.select ? *p.select : p.rest;
		}

		// AI is on, so keep an eye on standing characters wandering off.
		void Watch(std::uint32_t a_epoch)
		{
			if (a_epoch != g_epoch) {
				// Stale: Reset() already cleared g_watching, and a newer watcher
				// may be running, so leave the flag alone.
				return;
			}
			for (std::size_t i = 0; i < g_performers.size(); ++i) {
				auto& p = g_performers[i];
				// Something took a seated character out of its seat (seen a few
				// seconds after spawning; possibly a body mod refreshing them):
				// sit back down.
				if (auto* sitter = p.started && !p.busy && p.current.sit && p.furniture ? ActorOf(p) : nullptr;
					sitter && SitState(sitter) == RE::SIT_SLEEP_STATE::kNormal) {
					logger::warn("Poses: slot {} was taken out of its furniture ({}); sitting it back down", i + 1, Describe(sitter));
					Apply(i, p.current, "back into furniture");
					continue;
				}
				// Not sleepers: a head turning to follow the camera in its sleep.
				if (auto* performer = p.started && !p.current.sleep ? ActorOf(p) : nullptr) {
					LookAtCamera(performer);
				}
				auto* actor = p.started && !p.busy && !p.rest.sit ? ActorOf(p) : nullptr;
				if (!actor || SitState(actor) != RE::SIT_SLEEP_STATE::kNormal) {
					continue;
				}
				const auto pos = actor->GetPosition();
				const float dx = pos.x - p.home.x, dy = pos.y - p.home.y;
				if (std::sqrt(dx * dx + dy * dy) > kDriftLimit) {
					logger::warn("Poses: slot {} drifted to ({:.0f}, {:.0f}, {:.0f}), putting it back ({})", i + 1, pos.x, pos.y, pos.z, Describe(actor));
					actor->SetPosition(p.home, true);
					actor->SetAngle(RE::NiPoint3{ 0.0f, 0.0f, p.heading });
					SetTopPackage(actor->GetActorBase(), DoNothingPackage());
					actor->EvaluatePackage(true, false);
				}
			}
			RunAfter(kWatchInterval, [a_epoch]() { Watch(a_epoch); });
		}
	}

	std::optional<Pose> Parse(std::string_view a_text)
	{
		auto parsed = ParsePose(a_text);
		if (parsed.unknown) {
			logger::warn("Poses: unknown pose '{}' (use a built-in name or event:<AnimEventName>)", a_text);
		}
		return std::move(parsed.pose);
	}

	void Reset(bool a_gameOver)
	{
		++g_epoch;
		g_performers.clear();
		g_selected.reset();
		// The running watcher stops at its next tick (epoch changed); let the
		// next Start() schedule a fresh one even within that tick.
		g_watching = false;
		if (a_gameOver) {
			// Made in the lobby's game, and never freed by us: the pointer
			// means nothing in the next game, so make a new one there.
			g_doNothing = nullptr;
		}
	}

	void PrepareBase(RE::TESNPC* a_npc, const Pose& a_rest, bool a_hasFurniture)
	{
		auto* furniturePackage = a_rest.sit && a_hasFurniture ? FurniturePackage(a_rest) : nullptr;
		auto* package = furniturePackage ? furniturePackage : DoNothingPackage();
		SetTopPackage(a_npc, package);
		logger::debug("Poses: base {:08X} top package {:08X} ({} packages)", a_npc ? a_npc->GetFormID() : 0, package ? package->GetFormID() : 0,
			a_npc ? a_npc->aiPackages.packages.size() : 0);
	}

	void PrepareRef(RE::Actor* a_actor, RE::TESObjectREFR* a_furniture)
	{
		if (a_actor && a_furniture) {
			a_actor->extraList.SetLinkedRef(a_furniture, nullptr);
			// Before it is enabled: lets the engine put it straight into its
			// furniture while its 3D loads, the way NPCs are already seated
			// when a cell loads (without it, it walks over and may never sit).
			a_actor->GetActorRuntimeData().boolFlags.set(RE::Actor::BOOL_FLAGS::kAllowInstantFurniturePopInPlayerCell);
		}
	}

	bool IsSettled(std::size_t a_index)
	{
		auto* p = Get(a_index);
		return !p || (p->started && !p->busy);
	}

	bool InFurniture(RE::Actor* a_actor)
	{
		return SitState(a_actor) != RE::SIT_SLEEP_STATE::kNormal;
	}

	void Configure(std::size_t a_index, RE::ObjectRefHandle a_actor, RE::ObjectRefHandle a_furniture, const RE::NiPoint3& a_home,
		float a_heading, Pose a_rest, std::optional<Pose> a_select)
	{
		if (a_index >= g_performers.size()) {
			g_performers.resize(a_index + 1);
		}
		auto& p = g_performers[a_index];
		p.actor = a_actor;
		p.furniture = a_furniture;
		p.home = a_home;
		p.heading = a_heading;
		p.rest = std::move(a_rest);
		p.select = std::move(a_select);
		auto furniture = a_furniture.get();
		logger::debug("Poses: slot {} rest '{}' select '{}' furniture {:08X}", a_index + 1, p.rest.name, p.select ? p.select->name : "-",
			furniture ? furniture->GetFormID() : 0);
	}

	void Start(std::size_t a_index)
	{
		auto* p = Get(a_index);
		auto* actor = p ? ActorOf(*p) : nullptr;
		if (!actor) {
			return;
		}
		// The base's top package (PrepareBase) decides what it does from here.
		actor->EnableAI(true);
		actor->EvaluatePackage(true, false);
		if (!p->rest.sleep) {
			LookAtCamera(actor);
		}
		p->started = true;
		p->current = Pose{ "stand" };
		logger::debug("Poses: slot {} AI on ({})", a_index + 1, Describe(actor));
		Apply(a_index, Wanted(a_index), "start");
		if (!g_watching) {
			g_watching = true;
			const auto epoch = g_epoch;
			RunAfter(kWatchInterval, [epoch]() { Watch(epoch); });
		}
	}

	void Select(std::size_t a_index)
	{
		const auto previous = std::exchange(g_selected, a_index);
		if (previous == a_index) {
			return;
		}
		// Slots without a select pose stay as they are (re-sending a running
		// idle only gets it rejected).
		if (previous && *previous < g_performers.size() && g_performers[*previous].select) {
			Apply(*previous, g_performers[*previous].rest, "deselected");
		}
		if (a_index < g_performers.size() && g_performers[a_index].select) {
			Apply(a_index, *g_performers[a_index].select, "selected");
		}
	}

	void Reapply(std::size_t a_index)
	{
		if (a_index < g_performers.size()) {
			Apply(a_index, Wanted(a_index), "moved");
		}
	}

	bool IsSeated(std::size_t a_index)
	{
		auto* p = Get(a_index);
		return p && InFurniture(ActorOf(*p));
	}

	void Report(std::string_view a_when)
	{
		for (std::size_t i = 0; i < g_performers.size(); ++i) {
			const auto& p = g_performers[i];
			auto*       actor = ActorOf(p);
			if (!actor) {
				continue;
			}
			const auto pos = actor->GetPosition();
			logger::debug("Poses: {}: slot {} pose '{}' busy={} at ({:.0f}, {:.0f}, {:.0f}) {:.0f} from home; {}", a_when, i + 1, p.current.name,
				p.busy, pos.x, pos.y, pos.z, pos.GetDistance(p.home), Describe(actor));
		}
	}
}
