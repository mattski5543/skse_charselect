#pragma once

// Diagnostics for missing hands (0.9.1). A skin mod edits skins at
// runtime the first time it meets them; a skin first edited during the select
// screen renders men without hands for the rest of that launch, while the
// same edit made in the real game is fine. At fixed points around the lobby
// and the load after it, this logs every armor whose addon list changed since
// the main menu first opened, every NPC or race whose skin changed, and what
// loaded men wear in their hands and body slots. Logging only: it changes
// nothing in the game.

namespace charselect::skinaudit
{
	// Main thread. The first call records the baseline the later ones compare
	// against. With a_men, also logs every loaded male actor's hands and body.
	void Checkpoint(std::string_view a_when, bool a_men = false);

	// The same after a delay, on the main thread.
	void CheckpointAfter(std::chrono::seconds a_delay, std::string a_when, bool a_men = false);

	// For the next 15 minutes, logs each loaded man wearing one of a skin
	// mod's runtime-filled skins or a skin with no addons, or whose hands
	// have no 3D: the men who should show no hands. Restarts on each call.
	void StartWatch();
}
