// Hardware write breakpoints (see HwBreakpoints.h). Built without the plugin's
// precompiled header (see CMakeLists.txt) so it can include <windows.h>.

#include "HwBreakpoints.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <TlHelp32.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <filesystem>
#include <format>
#include <thread>

namespace charselect::hwbreak
{
	namespace
	{
		constexpr std::size_t kMaxHits = 16;
		constexpr std::size_t kMaxFrames = 28;

		// Filled by the exception handler: fixed storage, no allocation there.
		struct Hit
		{
			std::uint32_t  field{ 0 };
			std::uint32_t  threadId{ 0 };
			std::uint32_t  frames{ 0 };
			std::uint64_t  value{ 0 };
			std::uintptr_t stack[kMaxFrames]{};
		};

		std::array<Field, 4>       g_fields{};
		std::size_t                g_fieldCount{ 0 };
		std::array<Hit, kMaxHits>  g_hits{};
		std::atomic<std::uint32_t> g_seen{ 0 };
		void*                      g_handler{ nullptr };
		bool                       g_armed{ false };

		LONG CALLBACK OnException(EXCEPTION_POINTERS* a_info)
		{
			if (a_info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) {
				return EXCEPTION_CONTINUE_SEARCH;
			}
			auto*      ctx = a_info->ContextRecord;
			const auto fired = static_cast<std::uint32_t>(ctx->Dr6 & 0xF);
			if (!fired) {
				return EXCEPTION_CONTINUE_SEARCH;  // a real single step, not ours
			}
			ctx->Dr6 = 0;
			const auto field = static_cast<std::uint32_t>(std::countr_zero(fired));
			const auto slot = g_seen.fetch_add(1);
			if (slot >= kMaxHits || field >= g_fieldCount) {
				return EXCEPTION_CONTINUE_EXECUTION;
			}
			auto& hit = g_hits[slot];
			hit.field = field;
			hit.threadId = GetCurrentThreadId();
			const auto& f = g_fields[field];
			hit.value = f.size == 8 ? *reinterpret_cast<const std::uint64_t*>(f.address) : *reinterpret_cast<const std::uint32_t*>(f.address);

			// Walk the writer's stack from the trapped context, never reading
			// outside this thread's stack (a bad frame would fault in here).
			const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
			const auto  low = reinterpret_cast<DWORD64>(tib->StackLimit);
			const auto  high = reinterpret_cast<DWORD64>(tib->StackBase);
			CONTEXT walk = *ctx;
			for (hit.frames = 0; hit.frames < kMaxFrames && walk.Rip && walk.Rsp >= low && walk.Rsp + 8 <= high; ++hit.frames) {
				hit.stack[hit.frames] = walk.Rip;
				DWORD64 imageBase = 0;
				if (auto* fn = RtlLookupFunctionEntry(walk.Rip, &imageBase, nullptr)) {
					void*   handlerData = nullptr;
					DWORD64 establisher = 0;
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, walk.Rip, fn, &walk, &handlerData, &establisher, nullptr);
				} else {
					walk.Rip = *reinterpret_cast<const DWORD64*>(walk.Rsp);
					walk.Rsp += 8;
				}
			}
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		// Debug registers are per thread and can't be changed on a running
		// thread, so a helper thread suspends each thread in turn (the caller
		// waits on it) and writes them. The list is taken before any suspend.
		std::size_t SetDebugRegisters(bool a_arm)
		{
			std::uint64_t dr7 = 0;
			for (std::size_t i = 0; a_arm && i < g_fieldCount; ++i) {
				const std::uint64_t len = g_fields[i].size == 8 ? 0b10 : 0b11;
				dr7 |= 1ull << (i * 2);         // local enable
				dr7 |= 0b01ull << (16 + i * 4);  // break on write
				dr7 |= len << (18 + i * 4);
			}
			std::size_t done = 0;
			std::thread helper([&]() {
				std::vector<DWORD> threads;
				const auto         self = GetCurrentThreadId();
				if (HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); snap != INVALID_HANDLE_VALUE) {
					THREADENTRY32 te{};
					te.dwSize = sizeof(te);
					for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
						if (te.th32OwnerProcessID == GetCurrentProcessId() && te.th32ThreadID != self) {
							threads.push_back(te.th32ThreadID);
						}
					}
					CloseHandle(snap);
				}
				for (const auto tid : threads) {
					HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, tid);
					if (!h) {
						continue;
					}
					if (SuspendThread(h) != static_cast<DWORD>(-1)) {
						CONTEXT ctx{};
						ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
						if (GetThreadContext(h, &ctx)) {
							ctx.Dr0 = a_arm && g_fieldCount > 0 ? g_fields[0].address : 0;
							ctx.Dr1 = a_arm && g_fieldCount > 1 ? g_fields[1].address : 0;
							ctx.Dr2 = a_arm && g_fieldCount > 2 ? g_fields[2].address : 0;
							ctx.Dr3 = a_arm && g_fieldCount > 3 ? g_fields[3].address : 0;
							ctx.Dr6 = 0;
							ctx.Dr7 = dr7;
							if (SetThreadContext(h, &ctx)) {
								++done;
							}
						}
						ResumeThread(h);
					}
					CloseHandle(h);
				}
			});
			helper.join();
			return done;
		}

		std::string Where(std::uintptr_t a_address)
		{
			HMODULE module = nullptr;
			if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(a_address), &module)) {
				wchar_t path[MAX_PATH]{};
				GetModuleFileNameW(module, path, MAX_PATH);
				std::error_code ec;
				auto            name = std::filesystem::path(path).filename().u8string();
				return std::format("{}+{:07X}", std::string(name.begin(), name.end()), a_address - reinterpret_cast<std::uintptr_t>(module));
			}
			return std::format("{:X}", a_address);
		}
	}

	std::size_t Arm(const std::vector<Field>& a_fields)
	{
		if (g_armed || a_fields.empty()) {
			return 0;
		}
		g_fieldCount = (std::min)(a_fields.size(), g_fields.size());
		std::copy_n(a_fields.begin(), g_fieldCount, g_fields.begin());
		g_seen = 0;
		g_handler = AddVectoredExceptionHandler(1, OnException);
		g_armed = true;
		return SetDebugRegisters(true);
	}

	bool IsArmed()
	{
		return g_armed;
	}

	Report Disarm()
	{
		Report report;
		if (!g_armed) {
			return report;
		}
		report.threads = SetDebugRegisters(false);
		if (g_handler) {
			RemoveVectoredExceptionHandler(g_handler);
			g_handler = nullptr;
		}
		g_armed = false;
		report.seen = g_seen.load();
		const auto recorded = (std::min)(static_cast<std::size_t>(report.seen), kMaxHits);
		for (std::size_t i = 0; i < recorded; ++i) {
			const auto& hit = g_hits[i];
			Write w{ hit.field, hit.threadId, hit.value };
			for (std::uint32_t f = 0; f < hit.frames; ++f) {
				w.stack.push_back(Where(hit.stack[f]));
			}
			report.writes.push_back(std::move(w));
		}
		return report;
	}
}
