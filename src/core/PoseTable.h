#pragma once

// The built-in poses and the pose names in layout files, free of the game so
// they can be unit tested. Poses.cpp plays them.

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace charselect::poses
{
	struct Pose
	{
		std::string name;
		bool        sit{ false };          // in the slot's furniture
		bool        weaponDrawn{ false };
		std::string event;                 // graph event sent once in position
		bool        sleep{ false };        // with sit: lie down in it (bed, bedroll) instead of sitting
		std::string marker;                // furniture placed at the slot's spot when the slot names none ("0x...|Skyrim.esm")
	};

	// Built-in poses by name.
	[[nodiscard]] const std::map<std::string, Pose, std::less<>>& Table();

	struct Parsed
	{
		std::optional<Pose> pose;
		bool                unknown{ false };  // a_text named no pose (worth a warning)
	};
	// A built-in name (any case), "event:<AnimEventName>" (standing) or
	// "sit+event:<AnimEventName>" (seated). "none" and "" are no pose, quietly.
	[[nodiscard]] Parsed ParsePose(std::string_view a_text);

	// The poses the editor panel cycles through, in order. The select list
	// starts with "none" (stay in the rest pose) and adds one-shots.
	[[nodiscard]] const std::vector<std::string>& PickList(bool a_select);
}
