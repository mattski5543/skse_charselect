// Tickets for appearance exports (see ExportTracker.h).

#include "core/ExportTracker.h"

namespace charselect::sidecar
{
	void ExportTracker::OnSave(std::uint32_t a_characterId)
	{
		if (_current != a_characterId) {
			_current = a_characterId;
			++_epoch;
			_failures.clear();
		}
	}

	void ExportTracker::OnLoad(std::optional<std::uint32_t> a_characterId)
	{
		_current = a_characterId;
		++_epoch;
		_failures.clear();
	}

	std::optional<ExportTracker::Ticket> ExportTracker::Begin(std::uint32_t a_characterId) const
	{
		if (_current != a_characterId) {
			return std::nullopt;
		}
		return Ticket{ a_characterId, _epoch };
	}

	bool ExportTracker::IsCurrent(const Ticket& a_ticket) const
	{
		return a_ticket.epoch == _epoch && _current == a_ticket.characterId;
	}

	ExportTracker::Outcome ExportTracker::Failed(const Ticket& a_ticket)
	{
		if (!IsCurrent(a_ticket)) {
			return Outcome::kStale;
		}
		auto& count = _failures[a_ticket.characterId];
		if (++count > kMaxRetries) {
			_failures.erase(a_ticket.characterId);
			return Outcome::kGiveUp;
		}
		return Outcome::kRetry;
	}

	void ExportTracker::Succeeded(const Ticket& a_ticket)
	{
		if (IsCurrent(a_ticket)) {
			_failures.erase(a_ticket.characterId);
		}
	}

	std::vector<ExportTracker::Ticket> ExportTracker::Pending() const
	{
		std::vector<Ticket> out;
		for (const auto& [id, count] : _failures) {
			if (_current == id) {
				out.push_back({ id, _epoch });
			}
		}
		return out;
	}

	int ExportTracker::Failures(std::uint32_t a_characterId) const
	{
		const auto it = _failures.find(a_characterId);
		return it == _failures.end() ? 0 : it->second;
	}
}
