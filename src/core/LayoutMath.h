#pragma once

// The lobby's placement maths, free of the game so it can be unit tested:
// where characters stand when a cell's layout doesn't place them, and which
// seat a seated character takes. Skyrim headings: 0 faces +y (north),
// forward = (sin h, cos h).

#include "core/Geometry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace charselect::geometry
{
	constexpr float kPi = 3.14159265358979323846f;

	// The heading that faces a_to from a_from.
	[[nodiscard]] float YawTowards(const Vec3& a_from, const Vec3& a_to);
	// The same angle in [-pi, pi].
	[[nodiscard]] float WrapAngle(float a_rad);
	// Whole degrees in [0, 360), for the editor panel.
	[[nodiscard]] int Degrees(float a_rad);

	struct Placement
	{
		Vec3  pos;
		float heading{ 0.0f };
	};

	// Characters a layout doesn't place stand in a row in front of the camera,
	// facing it, for the player to arrange with the layout editor.
	constexpr float kRowDistance = 300.0f;  // ahead of the camera
	constexpr float kRowSpacing = 80.0f;    // between characters
	// Spot a_index of a_count in that row, slot order left to right as the
	// camera sees it, on the camera's floor.
	[[nodiscard]] Placement RowSpot(const Vec3& a_camera, float a_cameraYaw, std::size_t a_index, std::size_t a_count);
	// The middle of the row: the stage centre for a cell with no saved one.
	[[nodiscard]] Vec3 RowCentre(const Vec3& a_camera, float a_cameraYaw);

	struct Seat
	{
		Vec3          pos;  // where its character waits, just in front of it
		float         heading{ 0.0f };
		std::uint32_t marker{ 0 };
	};
	// The waiting spot kSeatFront in front of a sit marker at a_seat, facing
	// a_heading, on the furniture's floor.
	constexpr float kSeatFront = 40.0f;
	[[nodiscard]] Vec3 SeatFront(const Vec3& a_seat, float a_heading, float a_floorZ);
	// Of the free seats, the one nearest a_near if it's within a_keepRadius
	// (a saved spot picks its seat), else the one nearest the stage.
	constexpr float kKeepSavedSeat = 100.0f;
	[[nodiscard]] std::optional<Seat> ChooseSeat(const std::vector<Seat>& a_free, const Vec3& a_near, const Vec3& a_stage,
		float a_keepRadius = kKeepSavedSeat);

	// The next position in a list of a_count when stepping a_dir (+1/-1),
	// wrapping round. a_pos -1 (not in the list): starts at an end.
	[[nodiscard]] int CycleStep(int a_pos, int a_count, int a_dir);
}
