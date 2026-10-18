#pragma once

// A plain 3D point for the game-free code (the plugin converts to and from
// RE::NiPoint3 at the boundary).

#include <cmath>

namespace charselect
{
	struct Vec3
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };

		bool operator==(const Vec3&) const = default;

		Vec3 operator+(const Vec3& a_o) const { return { x + a_o.x, y + a_o.y, z + a_o.z }; }
		Vec3 operator-(const Vec3& a_o) const { return { x - a_o.x, y - a_o.y, z - a_o.z }; }
		Vec3 operator*(float a_s) const { return { x * a_s, y * a_s, z * a_s }; }
	};

	// Distance on the floor plane (ignores height).
	inline float FlatDistance(const Vec3& a_a, const Vec3& a_b)
	{
		return std::sqrt((a_a.x - a_b.x) * (a_a.x - a_b.x) + (a_a.y - a_b.y) * (a_a.y - a_b.y));
	}
}
