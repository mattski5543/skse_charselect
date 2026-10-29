#pragma once

// Small helpers used across the plugin: running game work as SKSE tasks
// under one lock, delayed callbacks, and catching errors before they reach
// the game.

#include "core/TimerQueue.h"

namespace charselect
{
	// An exception that escapes into the game terminates it. Everything the
	// game or SKSE calls (messages, tasks, event sinks, Papyrus and Scaleform
	// callbacks, menu overrides) runs through this, which logs instead.
	template <class F>
	void Guarded(std::string_view a_where, F&& a_fn) noexcept
	{
		try {
			std::forward<F>(a_fn)();
		} catch (const std::exception& e) {
			logger::error("{}: unexpected error: {}", a_where, e.what());
		} catch (...) {
			logger::error("{}: unexpected error", a_where);
		}
	}

	// One lock for everything that touches CharSelect's game state (the
	// lobby's slots and performers, ResetGuard's capture, the frontend's
	// flags). SKSE messages and menu events arrive on the game's main thread
	// and SKSE tasks run on its worker threads on AE, and logs show them
	// overlapping, so each of those holds this while it runs. Recursive: a
	// handler may call code that tasks also run.
	inline std::recursive_mutex& GameLock()
	{
		static std::recursive_mutex lock;
		return lock;
	}

	// An SKSE task, guarded and holding GameLock.
	inline void QueueTask(std::function<void()> a_fn)
	{
		SKSE::GetTaskInterface()->AddTask([fn = std::move(a_fn)]() {
			std::scoped_lock lock(GameLock());
			Guarded("task", fn);
		});
	}

	// An SKSE task without GameLock, for an engine call that may wait for the
	// main thread, which may itself be waiting for GameLock in an SKSE
	// message: starting a load (PreLoadGame follows on the main thread).
	inline void QueueTaskUnlocked(std::function<void()> a_fn)
	{
		SKSE::GetTaskInterface()->AddTask([fn = std::move(a_fn)]() { Guarded("task", fn); });
	}

	inline void QueueUITask(std::function<void()> a_fn)
	{
		SKSE::GetTaskInterface()->AddUITask([fn = std::move(a_fn)]() { Guarded("UI task", fn); });
	}

	// One timer thread for every delayed callback. Never destroyed:
	// nothing may touch it during static destruction at process exit.
	inline TimerQueue& Timers()
	{
		static auto* queue = new TimerQueue([](TimerQueue::Task a_fn) { QueueTask(std::move(a_fn)); });
		return *queue;
	}

	// Runs a_fn as an SKSE task (QueueTask) after a delay.
	inline void RunAfter(std::chrono::milliseconds a_delay, std::function<void()> a_fn)
	{
		Timers().Post(a_delay, std::move(a_fn));
	}

	struct PluginForm
	{
		const RE::TESFile* file{ nullptr };
		RE::FormID         localId{ 0 };
	};

	// The plugin that defines a form and its ID inside that plugin, taken from
	// the form ID's load-order index; TESDataHandler::LookupForm(localId, file)
	// finds it again. Not GetFile(0): that is the last plugin to edit the form
	// (USSEP, WACCF, ...), where LookupForm misses or finds a different form.
	inline std::optional<PluginForm> OwningPlugin(const RE::TESForm* a_form)
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		if (!a_form || a_form->IsDynamicForm() || !dh) {
			return std::nullopt;
		}
		const auto id = a_form->GetFormID();
		PluginForm out;
		if ((id >> 24) == 0xFE) {
			out.file = dh->LookupLoadedLightModByIndex(static_cast<std::uint16_t>((id >> 12) & 0xFFF));
			out.localId = id & 0xFFF;
		} else {
			out.file = dh->LookupLoadedModByIndex(static_cast<std::uint8_t>(id >> 24));
			out.localId = id & 0xFFFFFF;
		}
		return out.file ? std::optional(out) : std::nullopt;
	}
}
