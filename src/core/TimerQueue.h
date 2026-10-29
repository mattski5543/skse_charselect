#pragma once

// Delayed callbacks, all on one background thread. When one is due it's
// handed to a dispatcher; the plugin's dispatcher queues it as an SKSE task
// (Util.h). (A thread per callback was wasteful: the lobby checks on its
// characters several times a second.)

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace charselect
{
	class TimerQueue
	{
	public:
		using Clock = std::chrono::steady_clock;
		using Task = std::function<void()>;
		using Dispatcher = std::function<void(Task)>;

		explicit TimerQueue(Dispatcher a_dispatch);
		// Stops the thread and drops pending callbacks.
		~TimerQueue();
		TimerQueue(const TimerQueue&) = delete;
		TimerQueue& operator=(const TimerQueue&) = delete;

		// a_fn goes to the dispatcher a_delay from now. Callbacks due at the
		// same time keep the order they were posted in.
		void Post(std::chrono::milliseconds a_delay, Task a_fn);
		// No more callbacks are dispatched, pending or new. For process exit.
		void Shutdown();

		[[nodiscard]] std::size_t Pending() const;

	private:
		struct Entry
		{
			Clock::time_point due;
			std::uint64_t     order;
			Task              fn;

			bool operator>(const Entry& a_other) const
			{
				return due != a_other.due ? due > a_other.due : order > a_other.order;
			}
		};

		void Run();

		Dispatcher                                                   _dispatch;
		mutable std::mutex                                           _lock;
		std::condition_variable                                      _wake;
		std::priority_queue<Entry, std::vector<Entry>, std::greater<>> _entries;
		std::uint64_t                                                _nextOrder{ 0 };
		bool                                                         _stopping{ false };
		std::thread                                                  _thread;
	};
}
