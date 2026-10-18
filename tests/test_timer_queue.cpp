// Tests for the delayed-callback thread.

#include "core/TimerQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace charselect;
using namespace std::chrono_literals;

namespace
{
	// Runs dispatched callbacks at once and records the order.
	class Recorder
	{
	public:
		TimerQueue::Dispatcher Dispatcher()
		{
			return [this](TimerQueue::Task a_fn) {
				a_fn();
				std::scoped_lock lock(_lock);
				++_ran;
				_changed.notify_all();
			};
		}

		void Add(int a_value)
		{
			std::scoped_lock lock(_lock);
			_order.push_back(a_value);
		}

		// Waits (up to 5 s) for a_count callbacks to have run.
		bool WaitFor(std::size_t a_count)
		{
			std::unique_lock lock(_lock);
			return _changed.wait_for(lock, 5s, [&]() { return _ran >= a_count; });
		}

		std::vector<int> Order()
		{
			std::scoped_lock lock(_lock);
			return _order;
		}

	private:
		std::mutex              _lock;
		std::condition_variable _changed;
		std::vector<int>        _order;
		std::size_t             _ran{ 0 };
	};
}

TEST_CASE("Callbacks run in order of when they're due", "[timers]")
{
	Recorder   rec;
	TimerQueue timers(rec.Dispatcher());
	timers.Post(150ms, [&]() { rec.Add(3); });
	timers.Post(30ms, [&]() { rec.Add(1); });
	timers.Post(90ms, [&]() { rec.Add(2); });
	REQUIRE(rec.WaitFor(3));
	CHECK(rec.Order() == std::vector{ 1, 2, 3 });
	CHECK(timers.Pending() == 0);
}

TEST_CASE("Callbacks due together keep the order they were posted in", "[timers]")
{
	Recorder   rec;
	TimerQueue timers(rec.Dispatcher());
	for (int i = 0; i < 20; ++i) {
		timers.Post(0ms, [&rec, i]() { rec.Add(i); });
	}
	REQUIRE(rec.WaitFor(20));
	std::vector<int> expected(20);
	for (int i = 0; i < 20; ++i) {
		expected[i] = i;
	}
	CHECK(rec.Order() == expected);
}

TEST_CASE("A callback can post another (polling loops)", "[timers]")
{
	Recorder   rec;
	TimerQueue timers(rec.Dispatcher());
	std::function<void(int)> poll = [&](int a_n) {
		rec.Add(a_n);
		if (a_n < 5) {
			timers.Post(5ms, [&, a_n]() { poll(a_n + 1); });
		}
	};
	timers.Post(0ms, [&]() { poll(1); });
	REQUIRE(rec.WaitFor(5));
	CHECK(rec.Order() == std::vector{ 1, 2, 3, 4, 5 });
}

TEST_CASE("An earlier callback posted later still runs first", "[timers]")
{
	Recorder   rec;
	TimerQueue timers(rec.Dispatcher());
	timers.Post(400ms, [&]() { rec.Add(2); });
	timers.Post(20ms, [&]() { rec.Add(1); });  // must wake the sleeping thread
	REQUIRE(rec.WaitFor(1));
	CHECK(rec.Order() == std::vector{ 1 });
	REQUIRE(rec.WaitFor(2));
	CHECK(rec.Order() == std::vector{ 1, 2 });
}

TEST_CASE("Shutdown drops what's pending and refuses new callbacks", "[timers]")
{
	Recorder   rec;
	TimerQueue timers(rec.Dispatcher());
	timers.Post(10s, [&]() { rec.Add(1); });
	CHECK(timers.Pending() == 1);
	timers.Shutdown();
	CHECK(timers.Pending() == 0);
	timers.Post(0ms, [&]() { rec.Add(2); });
	CHECK(timers.Pending() == 0);
	std::this_thread::sleep_for(50ms);
	CHECK(rec.Order().empty());  // nothing ran
}

TEST_CASE("A throwing dispatcher doesn't stop the timer thread", "[timers]")
{
	Recorder   rec;
	auto       inner = rec.Dispatcher();
	bool       first = true;
	TimerQueue timers([&](TimerQueue::Task a_fn) {
		if (std::exchange(first, false)) {
			throw std::runtime_error("boom");
		}
		inner(std::move(a_fn));
	});
	timers.Post(0ms, [&]() { rec.Add(1); });
	timers.Post(20ms, [&]() { rec.Add(2); });
	REQUIRE(rec.WaitFor(1));
	CHECK(rec.Order() == std::vector{ 2 });
}

TEST_CASE("Destroying the queue with callbacks pending is safe", "[timers]")
{
	Recorder rec;
	{
		TimerQueue timers(rec.Dispatcher());
		timers.Post(10s, [&]() { rec.Add(1); });
	}
	CHECK(rec.Order().empty());
}
