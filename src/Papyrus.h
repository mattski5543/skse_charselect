#pragma once

// Calling Papyrus static functions (RaceMenu's CharGen) from C++.
// Calls are queued on the Papyrus VM and complete asynchronously; the
// callback runs on whatever thread the VM finishes on, so callbacks that
// touch game state should hop to the main thread with QueueTask (Util.h).

#include "Util.h"

namespace charselect::papyrus
{
	using ResultFn = std::function<void(const RE::BSScript::Variable& a_result)>;

	class CallbackFunctor : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		explicit CallbackFunctor(ResultFn a_fn) :
			_fn(std::move(a_fn)) {}

		void operator()(RE::BSScript::Variable a_result) override
		{
			if (_fn) {
				Guarded("Papyrus callback", [&]() { _fn(a_result); });
			}
		}

		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		ResultFn _fn;
	};

	// Returns false if the call could not be queued (VM missing, or the
	// script/function doesn't exist, e.g. RaceMenu not installed).
	template <class... Args>
	bool CallStatic(std::string_view a_class, std::string_view a_function, ResultFn a_onDone, Args... a_args)
	{
		auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
		if (!vm) {
			logger::error("Papyrus {}.{}: no VM", a_class, a_function);
			return false;
		}

		RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new CallbackFunctor(std::move(a_onDone)) };
		auto* args = RE::MakeFunctionArguments(std::move(a_args)...);

		const RE::BSFixedString cls{ std::string(a_class) };
		const RE::BSFixedString fn{ std::string(a_function) };
		const bool queued = vm->DispatchStaticCall(cls, fn, args, callback);
		if (queued) {
			logger::debug("Papyrus {}.{} queued", a_class, a_function);
		} else {
			logger::error("Papyrus {}.{} could not be dispatched (script or function missing?)", a_class, a_function);
		}
		return queued;
	}
}
