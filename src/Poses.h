#pragma once

// Poses for lobby characters: what each one does at rest and when selected.
//
// Characters run with AI on (with AI off the animation graph never updates
// and they stand in the arms-out reference stance). Each one has its own
// runtime actor base, so its package list is ours: the top package is a
// do-nothing package (stand here) or Skyrim.esm's defaultSitLinkedRefNoConv
// with the slot's furniture as the actor's linked ref (sit there). Without a
// package of their own actors fall back to DefaultMasterPackage, a sandbox
// (they sit in random chairs and wander off to the bar). Idles are
// vanilla animation graph events (what Debug.SendAnimationEvent sends), so no
// other mod is needed.
//
// Configured per slot in the layout files (see CharSelectLayouts.ini):
//   slotNpose = <pose>        at rest (default: stand)
//   slotNselect = <pose>      while selected (default: stay in the rest pose)
//   slotNfurniture = 0x000C8FC6|Skyrim.esm   a placed reference, used by sit/sleep poses
//   slotNfurniture = place:0x000B8371|Skyrim.esm   a furniture base, spawned at the slot's spot

#include "core/PoseTable.h"

namespace charselect::poses
{
	// ParsePose (core/PoseTable.h), logging unknown names: a built-in name
	// (stand, sit, sit_drink, drink, warm_hands, draw_weapon, sit_floor, kneel,
	// lie_down, lute, ...) or "event:<AnimEventName>". "none" and "" are
	// nullopt quietly.
	[[nodiscard]] std::optional<Pose> Parse(std::string_view a_text);

	// All of these run under GameLock (Util.h), like the lobby's.

	// Forget every performer. a_gameOver: the lobby's game is over (a load
	// succeeded, the main menu, a new lobby), so the do-nothing package made
	// in it is forgotten too. A load that may still be cancelled keeps it, so
	// a resumed lobby recognises its own package.
	void Reset(bool a_gameOver);
	// Before placing the actor: its base's top package (stand, sit or sleep).
	void PrepareBase(RE::TESNPC* a_npc, const Pose& a_rest, bool a_hasFurniture);
	// After placing it, while still disabled: links it to its furniture.
	void PrepareRef(RE::Actor* a_actor, RE::TESObjectREFR* a_furniture);
	// True while sitting down, seated or getting up; don't move it then.
	[[nodiscard]] bool InFurniture(RE::Actor* a_actor);
	// Started and not in the middle of sitting down or standing up (true for
	// slots without a performer).
	[[nodiscard]] bool IsSettled(std::size_t a_index);
	// Sets up slot a_index; call again to change its configuration.
	// a_home/a_heading is where it stands; standing characters that drift
	// away from it are put back.
	void Configure(std::size_t a_index, RE::ObjectRefHandle a_actor, RE::ObjectRefHandle a_furniture, const RE::NiPoint3& a_home,
		float a_heading, Pose a_rest, std::optional<Pose> a_select);
	// Called when the actor first gets AI: turns it on and starts the rest pose.
	void Start(std::size_t a_index);
	// Puts the newly selected slot into its select pose and the previous one back to rest.
	void Select(std::size_t a_index);
	// The same for one slot (the editor moved it).
	void Reapply(std::size_t a_index);

	[[nodiscard]] bool IsSeated(std::size_t a_index);
	// Logs each performer's package, sit state and position.
	void Report(std::string_view a_when);
}
