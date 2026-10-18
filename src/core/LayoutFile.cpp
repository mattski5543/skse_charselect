// Reading and writing the layout INI files (see LayoutFile.h).

#include "core/LayoutFile.h"

#include "core/Text.h"

#include <format>
#include <utility>

namespace charselect::layout
{
	namespace
	{
		std::optional<float> GetFloat(const IniSection& a_section, const std::string& a_key)
		{
			const auto it = a_section.find(a_key);
			if (it == a_section.end()) {
				return std::nullopt;
			}
			try {
				return std::stof(it->second);
			} catch (...) {
				return std::nullopt;
			}
		}

		std::optional<Vec3> GetPoint(const IniSection& a_section, std::string_view a_prefix)
		{
			const auto x = GetFloat(a_section, std::format("{}x", a_prefix));
			const auto y = GetFloat(a_section, std::format("{}y", a_prefix));
			const auto z = GetFloat(a_section, std::format("{}z", a_prefix));
			if (x && y && z) {
				return Vec3{ *x, *y, *z };
			}
			return std::nullopt;
		}

		std::string Get(const IniSection& a_section, const std::string& a_key)
		{
			const auto found = a_section.find(a_key);
			return found != a_section.end() ? found->second : std::string{};
		}
	}

	IniFile ParseIni(std::istream& a_in)
	{
		IniFile     file;
		std::string section;
		std::string line;
		bool        first = true;
		while (std::getline(a_in, line)) {
			if (std::exchange(first, false) && line.starts_with("\xEF\xBB\xBF")) {
				line.erase(0, 3);  // UTF-8 byte order mark
			}
			line = text::Trim(line);
			if (line.empty() || line.front() == ';') {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = text::Lower(line.substr(1, line.size() - 2));
				continue;
			}
			if (const auto eq = line.find('='); eq != std::string::npos) {
				file[section][text::Lower(text::Trim(line.substr(0, eq)))] = text::Trim(line.substr(eq + 1));
			}
		}
		return file;
	}

	void WriteIni(std::ostream& a_out, const IniFile& a_file)
	{
		a_out << "; CharSelect lobby layouts, written by the in-game layout editor (F8 in the lobby).\n";
		a_out << "; Delete a section to go back to the built-in layout for that location.\n";
		for (const auto& [name, section] : a_file) {
			a_out << "\n[" << name << "]\n";
			for (const auto& [key, value] : section) {
				a_out << key << " = " << value << "\n";
			}
		}
	}

	void Merge(IniFile& a_base, const IniFile& a_over)
	{
		for (const auto& [name, section] : a_over) {
			for (const auto& [key, value] : section) {
				a_base[name][key] = value;
			}
		}
	}

	CellLayout ReadCell(const IniFile& a_file, std::string_view a_cell)
	{
		CellLayout layout;
		const auto it = a_file.find(text::Lower(a_cell));
		if (it == a_file.end()) {
			return layout;
		}
		const auto& section = it->second;
		layout.camera = GetPoint(section, "camera");
		layout.cameraYaw = GetFloat(section, "camerayaw").value_or(0.0f);
		layout.cameraPitch = GetFloat(section, "camerapitch").value_or(0.0f);
		layout.stage = GetPoint(section, "stage");
		layout.hour = GetFloat(section, "hour");
		layout.weather = Get(section, "weather");
		for (std::size_t i = 0; i < kMaxSlots; ++i) {
			const auto prefix = std::format("slot{}", i + 1);
			if (auto pos = GetPoint(section, prefix)) {
				layout.slots[i] = CellLayout::SlotSpot{ *pos, GetFloat(section, prefix + "heading").value_or(0.0f) };
			}
			layout.poses[i] = { Get(section, prefix + "pose"), Get(section, prefix + "select"), Get(section, prefix + "furniture") };
		}
		return layout;
	}

	void SetCamera(IniFile& a_file, std::string_view a_cell, const Vec3& a_pos, float a_yaw, float a_pitch)
	{
		auto& section = a_file[text::Lower(a_cell)];
		section["camerax"] = std::format("{:.1f}", a_pos.x);
		section["cameray"] = std::format("{:.1f}", a_pos.y);
		section["cameraz"] = std::format("{:.1f}", a_pos.z);
		section["camerayaw"] = std::format("{:.4f}", a_yaw);
		section["camerapitch"] = std::format("{:.4f}", a_pitch);
	}

	void SetSlot(IniFile& a_file, std::string_view a_cell, std::size_t a_index, const Vec3& a_pos, float a_heading)
	{
		auto&      section = a_file[text::Lower(a_cell)];
		const auto prefix = std::format("slot{}", a_index + 1);
		section[prefix + "x"] = std::format("{:.1f}", a_pos.x);
		section[prefix + "y"] = std::format("{:.1f}", a_pos.y);
		section[prefix + "z"] = std::format("{:.1f}", a_pos.z);
		section[prefix + "heading"] = std::format("{:.4f}", a_heading);
	}

	void SetSlotSetting(IniFile& a_file, std::string_view a_cell, std::size_t a_index, std::string_view a_key, std::string_view a_value)
	{
		a_file[text::Lower(a_cell)][std::format("slot{}{}", a_index + 1, a_key)] = std::string(a_value);
	}

	void ClearCell(IniFile& a_file, std::string_view a_cell)
	{
		a_file.erase(text::Lower(a_cell));
	}
}
