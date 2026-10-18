#pragma once

// Keeps track of who the player is, and of appearance exports waiting to be
// retried. Every export reads the current player's face, so an export asked
// for one character must never run once the player has become someone else
// (after loading another save, say). Each request gets a ticket naming the
// character; a load, a save as another character or the main menu makes
// older tickets stale. Sidecar.cpp uses it under its lock.

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace charselect::sidecar
{
	class ExportTracker
	{
	public:
		static constexpr int kMaxRetries = 3;

		struct Ticket
		{
			std::uint32_t characterId{ 0 };
			std::uint32_t epoch{ 0 };

			bool operator==(const Ticket&) const = default;
		};

		// The player is a_characterId: they just saved. A different character
		// than before starts a new epoch.
		void OnSave(std::uint32_t a_characterId);
		// A save is loading: the player becomes its character, or nobody we
		// know (nullopt: a custom-named save). Pending retries are dropped.
		void OnLoad(std::optional<std::uint32_t> a_characterId);
		// Main menu, new game or the lobby: the player is nobody we know.
		void Forget() { OnLoad(std::nullopt); }

		[[nodiscard]] std::optional<std::uint32_t> Current() const { return _current; }

		// A ticket to export a_characterId now; nullopt if that isn't who the
		// player is.
		[[nodiscard]] std::optional<Ticket> Begin(std::uint32_t a_characterId) const;
		// The player is still the ticket's character, in the same epoch.
		[[nodiscard]] bool IsCurrent(const Ticket& a_ticket) const;

		enum class Outcome
		{
			kRetry,   // queued for a retry
			kGiveUp,  // too many failures: the lobby uses a flat skin tone
			kStale,   // the player changed since: ignored
		};
		// The ticket's tint export didn't produce a texture.
		Outcome Failed(const Ticket& a_ticket);
		// The ticket's tint export worked: no more retries for it.
		void Succeeded(const Ticket& a_ticket);

		// Retries that may run now: only the current character's, current epoch.
		[[nodiscard]] std::vector<Ticket> Pending() const;
		[[nodiscard]] bool                HasPending() const { return !_failures.empty(); }
		[[nodiscard]] int                 Failures(std::uint32_t a_characterId) const;

	private:
		std::optional<std::uint32_t>           _current;
		std::uint32_t                          _epoch{ 0 };
		std::unordered_map<std::uint32_t, int> _failures;  // this epoch only
	};
}
