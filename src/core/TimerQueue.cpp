// The timer thread behind delayed callbacks (see TimerQueue.h).

#include "core/TimerQueue.h"

namespace charselect
{
	TimerQueue::TimerQueue(Dispatcher a_dispatch) :
		_dispatch(std::move(a_dispatch))
	{
		_thread = std::thread([this]() { Run(); });
	}

	TimerQueue::~TimerQueue()
	{
		Shutdown();
		if (_thread.joinable()) {
			_thread.join();
		}
	}

	void TimerQueue::Post(std::chrono::milliseconds a_delay, Task a_fn)
	{
		{
			std::scoped_lock lock(_lock);
			if (_stopping) {
				return;
			}
			_entries.push({ Clock::now() + a_delay, _nextOrder++, std::move(a_fn) });
		}
		_wake.notify_one();
	}

	void TimerQueue::Shutdown()
	{
		{
			std::scoped_lock lock(_lock);
			_stopping = true;
			_entries = {};
		}
		_wake.notify_one();
	}

	std::size_t TimerQueue::Pending() const
	{
		std::scoped_lock lock(_lock);
		return _entries.size();
	}

	void TimerQueue::Run()
	{
		std::unique_lock lock(_lock);
		while (!_stopping) {
			if (_entries.empty()) {
				_wake.wait(lock);
				continue;
			}
			const auto due = _entries.top().due;
			if (Clock::now() < due) {
				_wake.wait_until(lock, due);
				continue;  // re-check: an earlier entry may have arrived, or a shutdown
			}
			auto fn = std::move(const_cast<Entry&>(_entries.top()).fn);
			_entries.pop();
			lock.unlock();
			try {
				_dispatch(std::move(fn));  // outside the lock: it may post again
			} catch (...) {
				// The dispatcher's problem; the timer thread must keep running.
			}
			lock.lock();
		}
	}
}
