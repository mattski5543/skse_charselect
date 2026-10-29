// Reads and writes lobby layouts, and turns the form references in them into
// game objects. The file format is in core/LayoutFile.cpp.

#include "Layout.h"

#include "Paths.h"
#include "Util.h"
#include "core/Encoding.h"
#include "core/FormKey.h"
#include "core/Files.h"
#include "core/Text.h"

#include <sstream>

namespace charselect::layout
{
	namespace
	{
		// Built-in layouts shipped with the mod; the user's file overrides them.
		std::filesystem::path DefaultsPath()
		{
			return std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "CharSelectLayouts.ini";
		}

		std::optional<std::filesystem::path> FilePath()
		{
			auto dir = logger::log_directory();
			if (!dir) {
				return std::nullopt;
			}
			return *dir / "CharSelect" / "Layout.ini";
		}

		IniFile ReadFile(const std::filesystem::path& a_path)
		{
			std::ifstream in(a_path);
			return ParseIni(in);
		}

		// The user's own layouts (the file the editor writes).
		IniFile Read()
		{
			const auto path = FilePath();
			return path ? ReadFile(*path) : IniFile{};
		}

		void Write(const IniFile& a_file)
		{
			const auto path = FilePath();
			if (!path) {
				return;
			}
			// Atomically: a crash mid-write must not lose the user's layouts.
			std::ostringstream ini;
			WriteIni(ini, a_file);
			std::string error;
			if (files::WriteAtomic(*path, ini.str(), error)) {
				logger::info("Layout: wrote {}", paths::ForLog(*path));
			} else {
				logger::error("Layout: could not write {}: {}", paths::ForLog(*path), error);
			}
		}

		RE::NiPoint3 ToNi(const Vec3& a_v) { return { a_v.x, a_v.y, a_v.z }; }
		Vec3         FromNi(const RE::NiPoint3& a_v) { return { a_v.x, a_v.y, a_v.z }; }
	}

	Saved Load(std::string_view a_cell)
	{
		// Built-in defaults first, then the user's keys on top.
		auto file = ReadFile(DefaultsPath());
		Merge(file, Read());
		const auto cell = ReadCell(file, a_cell);

		Saved saved;
		if (cell.camera) {
			saved.camera = ToNi(*cell.camera);
		}
		saved.cameraYaw = cell.cameraYaw;
		saved.cameraPitch = cell.cameraPitch;
		if (cell.stage) {
			saved.stage = ToNi(*cell.stage);
		}
		saved.hour = cell.hour;
		saved.weather = cell.weather;
		for (std::size_t i = 0; i < kMaxSlots; ++i) {
			if (const auto& spot = cell.slots[i]) {
				saved.slots[i] = Saved::SlotSpot{ ToNi(spot->pos), spot->heading };
			}
		}
		saved.poses = cell.poses;
		return saved;
	}

	RE::TESFurniture* ResolvePlaceable(std::string_view a_text)
	{
		const auto value = StripPlacePrefix(a_text);
		if (!value) {
			return nullptr;
		}
		const auto key = ParseFormKey(*value);
		auto*      dh = RE::TESDataHandler::GetSingleton();
		auto*      form = key && dh ? dh->LookupForm(key->localId, text::FromUtf8(key->plugin)) : nullptr;  // the INI is UTF-8
		auto*      furniture = form ? form->As<RE::TESFurniture>() : nullptr;
		if (!furniture) {
			logger::warn("Layout: '{}' is not a loaded furniture base", a_text);
		}
		return furniture;
	}

	RE::TESObjectREFR* ResolveRef(std::string_view a_text)
	{
		if (StripPlacePrefix(a_text)) {
			return nullptr;
		}
		if (a_text.find('|') == std::string_view::npos) {
			if (!a_text.empty() && text::Lower(a_text) != "none") {
				logger::warn("Layout: '{}' is not a reference (expected 0x000C8FC6|Skyrim.esm)", a_text);
			}
			return nullptr;
		}
		const auto key = ParseFormKey(a_text);
		if (!key) {
			logger::warn("Layout: bad form ID in '{}'", a_text);
			return nullptr;
		}
		auto* dh = RE::TESDataHandler::GetSingleton();
		auto* ref = dh ? dh->LookupForm<RE::TESObjectREFR>(key->localId, text::FromUtf8(key->plugin)) : nullptr;
		if (!ref) {
			logger::warn("Layout: reference '{}' not found (plugin not loaded?)", a_text);
		}
		return ref;
	}

	std::string FormatRef(const RE::TESObjectREFR* a_ref)
	{
		// Not GetFile(0): USSEP edits many Skyrim.esm references (0.5.3).
		const auto owner = OwningPlugin(a_ref);
		return owner ? FormatFormKey({ owner->localId, text::ToUtf8(owner->file->GetFilename()) }) : std::string{};
	}

	void SaveSlotSetting(std::string_view a_cell, std::size_t a_index, std::string_view a_key, std::string_view a_value)
	{
		auto file = Read();
		SetSlotSetting(file, a_cell, a_index, a_key, a_value);
		Write(file);
	}

	void SaveCamera(std::string_view a_cell, const RE::NiPoint3& a_pos, float a_yaw, float a_pitch)
	{
		auto file = Read();
		SetCamera(file, a_cell, FromNi(a_pos), a_yaw, a_pitch);
		Write(file);
	}

	void SaveSlot(std::string_view a_cell, std::size_t a_index, const RE::NiPoint3& a_pos, float a_heading)
	{
		auto file = Read();
		SetSlot(file, a_cell, a_index, FromNi(a_pos), a_heading);
		Write(file);
	}

	void Clear(std::string_view a_cell)
	{
		auto file = Read();
		ClearCell(file, a_cell);
		Write(file);
	}
}
