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
	void  SaveCamera(std::string_view a_cell, const RE::NiPoint3& a_pos, float a_yaw, float a_pitch);
	void  SaveSlot(std::string_view a_cell, std::size_t a_index, const RE::NiPoint3& a_pos, float a_heading);
	// slot<N><a_key> = a_value, e.g. "pose", "select", "furniture".
	void  SaveSlotSetting(std::string_view a_cell, std::size_t a_index, std::string_view a_key, std::string_view a_value);
	void  Clear(std::string_view a_cell);
}
