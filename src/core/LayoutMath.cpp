// Placement maths for the lobby (see LayoutMath.h).

#include "core/LayoutMath.h"

#include <algorithm>
#include <cmath>

namespace charselect::geometry
{
	float YawTowards(const Vec3& a_from, const Vec3& a_to)
	{
		return std::atan2(a_to.x - a_from.x, a_to.y - a_from.y);
	}

	float WrapAngle(float a_rad)
	{
		return std::remainder(a_rad, 2.0f * kPi);
	}

	int Degrees(float a_rad)
	{
		const int deg = static_cast<int>(std::lround(a_rad * 180.0f / kPi));
		return ((deg % 360) + 360) % 360;
	}

	Vec3 RowCentre(const Vec3& a_camera, float a_cameraYaw)
	{
		return a_camera + Vec3{ std::sin(a_cameraYaw), std::cos(a_cameraYaw), 0.0f } * kRowDistance;
	}

	Placement RowSpot(const Vec3& a_camera, float a_cameraYaw, std::size_t a_index, std::size_t a_count)
	{
		// The camera's right, on the floor: forward (sin, cos) turned 90 degrees clockwise.
		const Vec3  right{ std::cos(a_cameraYaw), -std::sin(a_cameraYaw), 0.0f };
		const float offset = (static_cast<float>(a_index) - (static_cast<float>(a_count) - 1.0f) / 2.0f) * kRowSpacing;
		Placement   spot;
		spot.pos = RowCentre(a_camera, a_cameraYaw) + right * offset;
		spot.heading = YawTowards(spot.pos, a_camera);
		return spot;
	}

	Vec3 SeatFront(const Vec3& a_seat, float a_heading, float a_floorZ)
	{
		Vec3 pos = a_seat + Vec3{ std::sin(a_heading), std::cos(a_heading), 0.0f } * kSeatFront;
		pos.z = a_floorZ;
		return pos;
	}

	std::optional<Seat> ChooseSeat(const std::vector<Seat>& a_free, const Vec3& a_near, const Vec3& a_stage, float a_keepRadius)
	{
		if (a_free.empty()) {
			return std::nullopt;
		}
		const auto nearest = [&](const Vec3& a_to) {
			return *std::ranges::min_element(a_free, {}, [&](const Seat& s) { return FlatDistance(s.pos, a_to); });
		};
		const auto closest = nearest(a_near);
		return FlatDistance(closest.pos, a_near) <= a_keepRadius ? closest : nearest(a_stage);
	}

	int CycleStep(int a_pos, int a_count, int a_dir)
	{
		if (a_count <= 0) {
			return -1;
		}
		return a_pos < 0 ? (a_dir > 0 ? 0 : a_count - 1) : ((a_pos + a_dir) % a_count + a_count) % a_count;
	}
}
