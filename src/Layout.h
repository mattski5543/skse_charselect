#pragma once

// Per-cell lobby layouts saved by the in-game layout editor, in
// Documents\My Games\...\SKSE\CharSelect\Layout.ini (user data, so Vortex/MO2
// never see a changed mod file). One section per cell editor ID. The file
// format lives in core/LayoutFile.h.

#include "core/LayoutFile.h"

namespace charselect::layout
{
	struct Saved
	{
		std::optional<RE::NiPoint3> camera;  // player feet position used as the camera
		float                       cameraYaw{ 0.0f };
		float                       cameraPitch{ 0.0f };
		std::optional<RE::NiPoint3> stage;  // where the characters gather
		std::optional<float>        hour;   // time of day in the lobby game
		std::string                 weather;  // a weather to force there, as a form key

		struct SlotSpot
		{
			RE::NiPoint3 pos;
			float        heading{ 0.0f };
		};
		std::array<std::optional<SlotSpot>, kMaxSlots> slots;  // exact spots for slots 1-9

		using SlotPose = CellLayout::SlotPose;
		std::array<SlotPose, kMaxSlots> poses;
	};

	// "0x000C8FC6|Skyrim.esm" -> the loaded reference, or nullptr.
	[[nodiscard]] RE::TESObjectREFR* ResolveRef(std::string_view a_text);
	// "place:0x000B8371|Skyrim.esm" -> that furniture base, or nullptr (not a place: value, or not loaded).
	[[nodiscard]] RE::TESFurniture* ResolvePlaceable(std::string_view a_text);
	// The inverse, for references from plugins; empty for runtime (FF) references.
	[[nodiscard]] std::string FormatRef(const RE::TESObjectREFR* a_ref);

	Saved Load(std::string_view a_cell);

	// One editor SAVE's changes to a cell's layout, written together.
	struct Changes
	{
		struct Camera
		{
			RE::NiPoint3 pos;
			float        yaw{ 0.0f };
			float        pitch{ 0.0f };
		};
		struct Spot
		{
			std::size_t      index{ 0 };
			Saved::SlotSpot  spot;
		};
		struct Setting  // slot<N><key> = value, e.g. "pose", "select", "furniture"
		{
			std::size_t index{ 0 };
			std::string key;
			std::string value;
		};
		std::optional<Camera> camera;
		std::vector<Spot>     spots;
		std::vector<Setting>  settings;

		[[nodiscard]] bool Empty() const { return !camera && spots.empty() && settings.empty(); }
	};

	// Each reads the file once and writes it once. False if nothing was
	// written (the file is there but can't be read, or can't be written;
	// both logged): the file is as it was.
	bool Save(std::string_view a_cell, const Changes& a_changes);
	bool Clear(std::string_view a_cell);
}
