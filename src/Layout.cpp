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

		bool Write(const std::filesystem::path& a_path, const IniFile& a_file)
		{
			// Atomically: a crash mid-write must not lose the user's layouts.
			std::ostringstream ini;
			WriteIni(ini, a_file);
			std::string error;
			if (!files::WriteAtomic(a_path, ini.str(), error)) {
				logger::error("Layout: could not write {}: {}", paths::ForLog(a_path), error);
				return false;
			}
			logger::info("Layout: wrote {}", paths::ForLog(a_path));
			return true;
		}

		// Reads the user's file, changes it and writes it back. A file that's
		// there but can't be read (locked by an editor or an antivirus) is left
		// alone: writing back what was read would replace every layout in it.
		template <class F>
		bool Update(F&& a_change)
		{
			const auto path = FilePath();
			if (!path) {
				logger::error("Layout: no folder for the layout file; not saved");
				return false;
			}
			const auto text = files::ReadForUpdate(*path);
			if (!text) {
				logger::warn("Layout: could not read {}; not saved, so the layouts in it are kept", paths::ForLog(*path));
				return false;
			}
			std::istringstream in(*text);
			auto               file = ParseIni(in);
			a_change(file);
			return Write(*path, file);
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
		// Not GetFile(0): that's the last plugin to edit the reference (USSEP edits
		// many of Skyrim.esm's), where the lookup would miss it.
		const auto owner = OwningPlugin(a_ref);
		return owner ? FormatFormKey({ owner->localId, text::ToUtf8(owner->file->GetFilename()) }) : std::string{};
	}

	bool Save(std::string_view a_cell, const Changes& a_changes)
	{
		return Update([&](IniFile& a_file) {
			if (a_changes.camera) {
				SetCamera(a_file, a_cell, FromNi(a_changes.camera->pos), a_changes.camera->yaw, a_changes.camera->pitch);
			}
			for (const auto& [index, spot] : a_changes.spots) {
				SetSlot(a_file, a_cell, index, FromNi(spot.pos), spot.heading);
			}
			for (const auto& [index, key, value] : a_changes.settings) {
				SetSlotSetting(a_file, a_cell, index, key, value);
			}
		});
	}

	bool Clear(std::string_view a_cell)
	{
		return Update([&](IniFile& a_file) { ClearCell(a_file, a_cell); });
	}
}
