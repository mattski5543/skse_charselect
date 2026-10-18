#pragma once

// The layout INI files, free of the game so they can be unit tested:
// CharSelectLayouts.ini (shipped defaults) and the user's Layout.ini (what
// the editor writes), one section per cell editor ID. Layout.cpp does the
// file paths and form lookups.

#include "core/Geometry.h"

#include <array>
#include <cstddef>
#include <istream>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace charselect::layout
{
	constexpr std::size_t kMaxSlots = 9;

	using IniSection = std::map<std::string, std::string>;
	using IniFile = std::map<std::string, IniSection>;  // section and key names lower-cased

	// Lines starting with ';' are comments; there are no inline comments
	// (values hold '|' and ':'). Later keys replace earlier ones.
	[[nodiscard]] IniFile ParseIni(std::istream& a_in);
	// With a header comment, sections and keys in sorted order.
	void WriteIni(std::ostream& a_out, const IniFile& a_file);
	// a_over's keys on top of a_base's, key by key.
	void Merge(IniFile& a_base, const IniFile& a_over);

	// One cell's layout.
	struct CellLayout
	{
		std::optional<Vec3> camera;  // player feet position used as the camera
		float               cameraYaw{ 0.0f };
		float               cameraPitch{ 0.0f };
		std::optional<Vec3> stage;  // where the characters gather
		std::optional<float> hour;   // time of day in the lobby game, 0-24
		std::string          weather;  // a weather to force there: "0x0010A244|Skyrim.esm"

		struct SlotSpot
		{
			Vec3  pos;
			float heading{ 0.0f };
		};
		std::array<std::optional<SlotSpot>, kMaxSlots> slots;  // exact spots for slots 1-9

		// Raw pose settings per slot (see Poses.h); empty when not set.
		struct SlotPose
		{
			std::string pose;       // slotNpose
			std::string select;     // slotNselect
			std::string furniture;  // slotNfurniture: "0x000C8FC6|Skyrim.esm", "place:...", or "none"
		};
		std::array<SlotPose, kMaxSlots> poses;
	};

	// a_cell's section of a_file (cell names ignore case); empty if none.
	[[nodiscard]] CellLayout ReadCell(const IniFile& a_file, std::string_view a_cell);

	// Edits, as the editor's SAVE writes them. a_index is 0-based (slot 1 = 0).
	void SetCamera(IniFile& a_file, std::string_view a_cell, const Vec3& a_pos, float a_yaw, float a_pitch);
	void SetSlot(IniFile& a_file, std::string_view a_cell, std::size_t a_index, const Vec3& a_pos, float a_heading);
	// slot<N><a_key> = a_value, e.g. "pose", "select", "furniture".
	void SetSlotSetting(IniFile& a_file, std::string_view a_cell, std::size_t a_index, std::string_view a_key, std::string_view a_value);
	void ClearCell(IniFile& a_file, std::string_view a_cell);
}
