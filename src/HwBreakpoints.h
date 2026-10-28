#pragma once

// Hardware write breakpoints (x64 debug registers) on up to four fields of this
// process, set on every thread; each write is recorded with the writer's call
// stack. Built without the precompiled header (it needs <windows.h>; see
// CMakeLists.txt), so only standard types cross this boundary.

#include <cstdint>
#include <string>
#include <vector>

namespace charselect::hwbreak
{
	struct Field
	{
		std::uintptr_t address{ 0 };
		std::uint32_t  size{ 0 };  // 4 or 8, and the address aligned to it
	};

	struct Write
	{
		std::size_t              field{ 0 };     // index into the fields armed
		std::uint32_t            threadId{ 0 };
		std::uint64_t            value{ 0 };     // the field just after the write
		std::vector<std::string> stack;          // "module+offset", writer first
	};

	struct Report
	{
		std::vector<Write> writes;
		std::uint32_t      seen{ 0 };     // including those not recorded
		std::size_t        threads{ 0 };  // threads the breakpoints were cleared on
	};

	// Returns the number of threads armed (0 if already armed or no fields).
	std::size_t Arm(const std::vector<Field>& a_fields);
	[[nodiscard]] bool IsArmed();
	Report Disarm();
}
