#pragma once

// Small helpers used across the plugin: running code on the game's main
// thread, delayed callbacks, and catching errors before they reach the game.

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

	// SKSE tasks, guarded.
	inline void QueueTask(std::function<void()> a_fn)
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

	// Runs a_fn on the main thread (as an SKSE task) after a delay.
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
