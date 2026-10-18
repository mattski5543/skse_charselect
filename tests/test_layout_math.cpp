// Tests for where characters stand without a saved spot, which seat they take, and the angle helpers.

#include "core/LayoutMath.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>

using namespace charselect;
using namespace charselect::geometry;
using Catch::Matchers::WithinAbs;

namespace
{
	constexpr float kEps = 1e-4f;
}

TEST_CASE("Headings follow Skyrim's convention (0 = north, clockwise)", "[geometry]")
{
	const Vec3 o{};
	CHECK_THAT(YawTowards(o, { 0, 10, 0 }), WithinAbs(0.0, kEps));
	CHECK_THAT(YawTowards(o, { 10, 0, 0 }), WithinAbs(kPi / 2, kEps));
	CHECK_THAT(YawTowards(o, { -10, 0, 0 }), WithinAbs(-kPi / 2, kEps));
	CHECK_THAT(std::abs(YawTowards(o, { 0, -10, 0 })), WithinAbs(kPi, kEps));
	CHECK_THAT(YawTowards(o, { 0, 10, 500 }), WithinAbs(0.0, kEps));  // height doesn't matter
	// Forward for a heading h is (sin h, cos h).
	const float h = YawTowards({ 1, 2, 0 }, { 4, 6, 0 });
	CHECK_THAT(std::sin(h), WithinAbs(0.6, kEps));
	CHECK_THAT(std::cos(h), WithinAbs(0.8, kEps));
}

TEST_CASE("WrapAngle and Degrees", "[geometry]")
{
	CHECK_THAT(WrapAngle(0.5f), WithinAbs(0.5, kEps));
	CHECK_THAT(WrapAngle(2 * kPi + 0.1f), WithinAbs(0.1, kEps));
	CHECK_THAT(WrapAngle(-2 * kPi - 0.1f), WithinAbs(-0.1, kEps));
	CHECK_THAT(std::abs(WrapAngle(3 * kPi)), WithinAbs(kPi, kEps));
	for (float a = -10.0f; a < 10.0f; a += 0.37f) {
		const float w = WrapAngle(a);
		CHECK(w >= -kPi - kEps);
		CHECK(w <= kPi + kEps);
		CHECK_THAT(std::sin(w), WithinAbs(std::sin(a), 1e-3));
		CHECK_THAT(std::cos(w), WithinAbs(std::cos(a), 1e-3));
	}

	CHECK(Degrees(0.0f) == 0);
	CHECK(Degrees(kPi / 2) == 90);
	CHECK(Degrees(kPi) == 180);
	CHECK(Degrees(-kPi / 2) == 270);
	CHECK(Degrees(2 * kPi) == 0);
	CHECK(Degrees(-0.001f) == 0);
	CHECK(Degrees(7 * kPi / 4) == 315);
	CHECK(Degrees(-4 * kPi - kPi / 2) == 270);
}

TEST_CASE("The default row in front of the camera", "[geometry][row]")
{
	const Vec3 camera{ 100.0f, 200.0f, 64.0f };

	SECTION("one character stands straight ahead, facing the camera")
	{
		const auto spot = RowSpot(camera, 0.0f, 0, 1);  // camera looking north
		CHECK_THAT(spot.pos.x, WithinAbs(100.0, kEps));
		CHECK_THAT(spot.pos.y, WithinAbs(200.0 + kRowDistance, kEps));
		CHECK_THAT(spot.pos.z, WithinAbs(64.0, kEps));  // on the camera's floor
		CHECK_THAT(std::abs(spot.heading), WithinAbs(kPi, kEps));
	}
	SECTION("several are spaced evenly, slot 1 on the camera's left")
	{
		std::vector<Placement> row;
		for (std::size_t i = 0; i < 4; ++i) {
			row.push_back(RowSpot(camera, 0.0f, i, 4));
		}
		for (std::size_t i = 0; i < 4; ++i) {
			INFO("slot " << i + 1);
			CHECK_THAT(row[i].pos.y, WithinAbs(200.0 + kRowDistance, kEps));  // all at the same distance ahead
			CHECK_THAT(row[i].heading, WithinAbs(YawTowards(row[i].pos, camera), kEps));
			if (i > 0) {
				CHECK_THAT(row[i].pos.x - row[i - 1].pos.x, WithinAbs(kRowSpacing, kEps));  // left to right
			}
		}
		// Centred on the camera's line of sight.
		CHECK_THAT(row[0].pos.x + row[3].pos.x, WithinAbs(200.0, 1e-3));
	}
	SECTION("the row turns with the camera")
	{
		const auto ahead = RowSpot(camera, kPi / 2, 0, 1);  // camera looking east
		CHECK_THAT(ahead.pos.x, WithinAbs(100.0 + kRowDistance, 1e-3));
		CHECK_THAT(ahead.pos.y, WithinAbs(200.0, 1e-3));
		const auto first = RowSpot(camera, kPi / 2, 0, 2);
		CHECK(first.pos.y > 200.0f);  // looking east, the camera's left is north
	}
	SECTION("its middle is the stage centre")
	{
		const auto centre = RowCentre(camera, 0.0f);
		CHECK(centre == RowSpot(camera, 0.0f, 0, 1).pos);
		CHECK_THAT(FlatDistance(centre, camera), WithinAbs(kRowDistance, 1e-3));
	}
}

TEST_CASE("Cycling through the editor's lists", "[geometry]")
{
	CHECK(CycleStep(0, 3, 1) == 1);
	CHECK(CycleStep(2, 3, 1) == 0);    // wraps forward
	CHECK(CycleStep(0, 3, -1) == 2);   // wraps back
	CHECK(CycleStep(-1, 3, 1) == 0);   // a custom value: start at the front
	CHECK(CycleStep(-1, 3, -1) == 2);  // or the back
	CHECK(CycleStep(0, 1, 1) == 0);
	CHECK(CycleStep(0, 1, -1) == 0);
	CHECK(CycleStep(0, 0, 1) == -1);   // an empty list has no position
	CHECK(CycleStep(-1, 0, -1) == -1);
}

TEST_CASE("Seats", "[geometry][seats]")
{
	SECTION("a character waits just in front of the seat, on the furniture's floor")
	{
		const auto pos = SeatFront({ 100.0f, 100.0f, 80.0f }, kPi / 2, 64.0f);  // facing east
		CHECK_THAT(pos.x, WithinAbs(100.0 + kSeatFront, 1e-3));
		CHECK_THAT(pos.y, WithinAbs(100.0, 1e-3));
		CHECK(pos.z == 64.0f);
	}

	const Vec3              stage{ 0.0f, 0.0f, 0.0f };
	const std::vector<Seat> bench{
		{ { 300.0f, 0.0f, 0.0f }, 0.0f, 0 },  // far end of the bench
		{ { 100.0f, 0.0f, 0.0f }, 0.0f, 1 },  // near the stage
	};
	SECTION("a saved spot near a seat keeps that seat")
	{
		const auto seat = ChooseSeat(bench, { 290.0f, 30.0f, 0.0f }, stage);
		REQUIRE(seat);
		CHECK(seat->marker == 0);
	}
	SECTION("otherwise the seat nearest the stage")
	{
		const auto seat = ChooseSeat(bench, { 1000.0f, 1000.0f, 0.0f }, stage);
		REQUIRE(seat);
		CHECK(seat->marker == 1);
	}
	SECTION("no free seats")
	{
		CHECK_FALSE(ChooseSeat({}, stage, stage));
	}
	SECTION("height doesn't count")
	{
		const auto seat = ChooseSeat(bench, { 300.0f, 0.0f, 500.0f }, stage);
		REQUIRE(seat);
		CHECK(seat->marker == 0);
	}
}
