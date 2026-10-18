// The built-in poses and pose names (see PoseTable.h).

#include "core/PoseTable.h"

#include "core/Text.h"

namespace charselect::poses
{
	const std::map<std::string, Pose, std::less<>>& Table()
	{
		static const std::map<std::string, Pose, std::less<>> table{
			{ "stand", { "stand", false, false, "" } },
			{ "sit", { "sit", true, false, "" } },
			{ "sit_drink", { "sit_drink", true, false, "ChairDrinkingStart" } },
			{ "sit_eat", { "sit_eat", true, false, "ChairEatingStart" } },
			{ "drink", { "drink", false, false, "idleDrinkingStandingStart" } },
			{ "warm_hands", { "warm_hands", false, false, "IdleWarmHandsStanding" } },
			{ "warm_hands_crouched", { "warm_hands_crouched", false, false, "IdleWarmHandsCrouched" } },
			{ "warm_arms", { "warm_arms", false, false, "IdleWarmArms" } },
			{ "hands_behind_back", { "hands_behind_back", false, false, "IdleHandsBehindBack" } },
			{ "salute", { "salute", false, false, "IdleSalute" } },
			{ "pray", { "pray", false, false, "IdlePray" } },
			{ "study", { "study", false, false, "IdleStudy" } },
			{ "look_far", { "look_far", false, false, "IdleLookFar" } },
			{ "applaud", { "applaud", false, false, "IdleApplaud2" } },
			{ "draw_weapon", { "draw_weapon", false, true, "" } },
			{ "sleep", { "sleep", true, false, "", true } },
			// Instruments and a book: the behaviour graph brings the prop.
			{ "lute", { "lute", false, false, "IdleLuteStart" } },
			{ "flute", { "flute", false, false, "IdleFluteStart" } },
			{ "drum", { "drum", false, false, "IdleDrumStart" } },
			{ "read", { "read", false, false, "IdleBook_Read" } },
			{ "sit_read", { "sit_read", true, false, "ChairReadingStart" } },
			// One-shots: good as select reactions.
			{ "wave", { "wave", false, false, "IdleWave" } },
			{ "laugh", { "laugh", false, false, "IdleLaugh" } },
			// Vanilla sit-type markers (invisible furniture), placed at the
			// slot's spot unless slotNfurniture names something else.
			{ "sit_floor", { "sit_floor", true, false, "", false, "0x000C4EFD|Skyrim.esm" } },          // SitCrossLeggedMarker
			{ "kneel", { "kneel", true, false, "", false, "0x000E8E4E|Skyrim.esm" } },                  // KneelingMarker
			{ "pray_kneel", { "pray_kneel", true, false, "", false, "0x000F11F1|Skyrim.esm" } },        // BlessingKneelMarker
			{ "pray_crouched", { "pray_crouched", true, false, "", false, "0x000F11E9|Skyrim.esm" } },  // PrayCrouchedMarker
			{ "lie_down", { "lie_down", true, false, "", false, "0x000C482E|Skyrim.esm" } },            // LayDownMarker
			{ "wounded", { "wounded", true, false, "", false, "0x000D8738|Skyrim.esm" } },              // Wounded01FloorMarker
			{ "lean_table", { "lean_table", true, false, "", false, "0x000C4EF1|Skyrim.esm" } },        // LeanTableMarker (needs a table)
			{ "lean_counter", { "lean_counter", true, false, "", false, "0x0006CF36|Skyrim.esm" } },    // CounterLeanMarker (needs a counter)
			{ "sit_ledge", { "sit_ledge", true, false, "", false, "0x000C4827|Skyrim.esm" } },          // SitLedgeMarker (needs a ledge)
		};
		return table;
	}

	const std::vector<std::string>& PickList(bool a_select)
	{
		// Standing, then on the floor (markers placed at the spot), then in
		// furniture, then one-shots. Leaning and ledge poses need geometry
		// at the spot, so they stay INI-only.
		static const std::vector<std::string> rest{ "stand", "warm_hands", "warm_hands_crouched", "warm_arms", "hands_behind_back", "drink",
			"read", "lute", "flute", "drum", "pray", "study", "look_far", "draw_weapon", "sit_floor", "kneel", "pray_kneel", "pray_crouched",
			"lie_down", "wounded", "sit", "sit_drink", "sit_eat", "sit_read", "sleep" };
		static const std::vector<std::string> select = [] {
			std::vector<std::string> list{ "none" };
			list.insert(list.end(), rest.begin(), rest.end());
			list.insert(list.end(), { "salute", "applaud", "wave", "laugh" });
			return list;
		}();
		return a_select ? select : rest;
	}

	Parsed ParsePose(std::string_view a_text)
	{
		const auto text = text::Lower(a_text);
		if (text.empty() || text == "none") {
			return {};
		}
		// Event names: keep what the user wrote.
		if (text.starts_with("event:")) {
			return { Pose{ std::string(a_text), false, false, std::string(a_text.substr(6)) } };
		}
		if (text.starts_with("sit+event:")) {
			return { Pose{ std::string(a_text), true, false, std::string(a_text.substr(10)) } };
		}
		const auto& table = Table();
		if (const auto it = table.find(text); it != table.end()) {
			return { it->second };
		}
		return { std::nullopt, true };
	}
}
