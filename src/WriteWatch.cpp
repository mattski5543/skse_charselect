// Which engine code empties records when the lobby's game ends (see
// WriteWatch.h). The breakpoints themselves are in HwBreakpoints.cpp.

#include "WriteWatch.h"

#include "HwBreakpoints.h"

namespace charselect::writewatch
{
	namespace
	{
		constexpr RE::FormID kWatchedNpc = 0x1A670;  // Mikael, Skyrim.esm

		std::vector<std::string> g_labels;
	}

	void Arm()
	{
		if (hwbreak::IsArmed()) {
			return;
		}
		auto* npc = RE::TESForm::LookupByID<RE::TESNPC>(kWatchedNpc);
		if (!npc) {
			logger::info("WriteWatch: {:08X} not found; not armed", kWatchedNpc);
			return;
		}
		std::vector<hwbreak::Field> fields;
		g_labels.clear();
		auto add = [&](const void* a_field, std::uint32_t a_size, std::string a_label) {
			const auto address = reinterpret_cast<std::uintptr_t>(a_field);
			if (address % a_size == 0) {
				fields.push_back({ address, a_size });
				g_labels.push_back(std::move(a_label));
			}
		};
		add(&npc->actorEffects, 8, "actorEffects (spell list pointer)");
		if (npc->actorEffects) {
			add(&npc->actorEffects->numSpells, 4, "actorEffects->numSpells");
		}
		add(&npc->numKeywords, 4, "numKeywords");
		add(&npc->perkCount, 4, "perkCount");
		const auto threads = hwbreak::Arm(fields);
		logger::info("WriteWatch: watching {} fields of {:08X} '{}' (spells {}, keywords {}, perks {}) on {} threads", fields.size(), kWatchedNpc,
			npc->GetName() ? npc->GetName() : "", npc->actorEffects ? npc->actorEffects->numSpells : 0, npc->numKeywords, npc->perkCount, threads);
	}

	void DisarmAndReport(std::string_view a_when)
	{
		if (!hwbreak::IsArmed()) {
			return;
		}
		const auto report = hwbreak::Disarm();
		logger::info("WriteWatch: {}: {} writes seen, {} recorded (cleared on {} threads)", a_when, report.seen, report.writes.size(), report.threads);
		for (std::size_t i = 0; i < report.writes.size(); ++i) {
			const auto& w = report.writes[i];
			logger::info("WriteWatch:   write {} to {} (now {:X}) on thread {}:", i + 1, w.field < g_labels.size() ? g_labels[w.field] : "?", w.value,
				w.threadId);
			for (const auto& frame : w.stack) {
				logger::info("WriteWatch:     {}", frame);
			}
		}
	}
}
