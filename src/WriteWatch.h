#pragma once

// Diagnostics (0.9.5): which engine code empties records when the lobby's game
// ends (see ResetGuard.h). Hardware write breakpoints on a few fields of a
// record known to be emptied (Mikael, Skyrim.esm 0001A670, from the Bannered
// Mare): every write to them while armed is logged with the writing code's
// call stack. Logging only; remove once the cause is known.

namespace charselect::writewatch
{
	// Arms the breakpoints on every thread. Main thread; no-op if armed.
	void Arm();

	// Disarms them and logs every write seen. Main thread; no-op if not armed.
	void DisarmAndReport(std::string_view a_when);
}
