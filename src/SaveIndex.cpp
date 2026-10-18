// Reads just the header of each save file, so characters can be listed
// without loading anything. Also used by tools/savescan and the unit tests.

#include "SaveIndex.h"

#include "core/Encoding.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <format>
#include <fstream>
#include <unordered_map>

namespace charselect
{
	namespace
	{
		constexpr std::string_view kMagic = "TESV_SAVEGAME";

		// The header is ~80 bytes plus three short strings; 1 KiB covers any
		// sane name/location while keeping the scan to one small read per file.
		constexpr std::size_t kHeaderReadSize = 1024;

		class Reader
		{
		public:
			Reader(const char* a_data, std::size_t a_size, unsigned a_codePage = text::kGameCodePage) :
				_data(a_data), _size(a_size), _codePage(a_codePage) {}

			template <class T>
			bool Read(T& a_out)
			{
				if (_pos + sizeof(T) > _size) {
					return false;
				}
				std::memcpy(&a_out, _data + _pos, sizeof(T));
				_pos += sizeof(T);
				return true;
			}

			// Bethesda "wstring": uint16 length + bytes (not null-terminated).
			bool ReadWString(std::string& a_out)
			{
				std::uint16_t len = 0;
				if (!Read(len) || _pos + len > _size) {
					return false;
				}
				a_out = text::ToUtf8({ _data + _pos, len }, _codePage);
				_pos += len;
				return true;
			}

			bool Skip(std::size_t a_bytes)
			{
				if (_pos + a_bytes > _size) {
					return false;
				}
				_pos += a_bytes;
				return true;
			}

			[[nodiscard]] std::size_t Pos() const { return _pos; }

		private:
			const char* _data;
			std::size_t _size;
			std::size_t _pos{ 0 };
			unsigned    _codePage;
		};

		bool ParseHex32(std::string_view a_text, std::uint32_t& a_out)
		{
			if (a_text.empty() || a_text.size() > 8) {
				return false;
			}
			auto [ptr, ec] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_out, 16);
			return ec == std::errc{} && ptr == a_text.data() + a_text.size();
		}

		bool ParseDec32(std::string_view a_text, std::uint32_t& a_out)
		{
			if (a_text.empty()) {
				return false;
			}
			auto [ptr, ec] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), a_out, 10);
			return ec == std::errc{} && ptr == a_text.data() + a_text.size();
		}

		// ".ess" in any case, compared as UTF-16: path::string() throws for
		// names the ANSI code page can't hold.
		bool IsEss(const std::filesystem::path& a_path)
		{
			const auto ext = a_path.extension().native();
			return ext.size() == 4 && ext[0] == L'.' && std::towlower(ext[1]) == L'e' && std::towlower(ext[2]) == L's' && std::towlower(ext[3]) == L's';
		}

		bool IEquals(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() &&
			       std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
					   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
				   });
		}
	}

	std::optional<SaveHeader> ReadSaveHeader(const std::filesystem::path& a_path, std::string& a_error, unsigned a_codePage)
	{
		std::ifstream file(a_path, std::ios::binary);
		if (!file) {
			a_error = "cannot open";
			return std::nullopt;
		}

		std::array<char, kHeaderReadSize> buf{};
		file.read(buf.data(), buf.size());
		const auto got = static_cast<std::size_t>(file.gcount());

		Reader r(buf.data(), got);
		if (got < kMagic.size() || std::string_view(buf.data(), kMagic.size()) != kMagic) {
			a_error = "bad magic (not a Skyrim save)";
			return std::nullopt;
		}
		r.Skip(kMagic.size());

		std::uint32_t headerSize = 0;
		if (!r.Read(headerSize)) {
			a_error = "truncated before header size";
			return std::nullopt;
		}
		if (r.Pos() + headerSize > got) {
			a_error = std::format("header size {} exceeds read buffer ({} bytes)", headerSize, got);
			return std::nullopt;
		}

		// Restrict the reader to exactly the declared header.
		Reader h(buf.data() + r.Pos(), headerSize, a_codePage);
		SaveHeader out;
		const bool ok =
			h.Read(out.version) &&
			h.Read(out.saveNumber) &&
			h.ReadWString(out.playerName) &&
			h.Read(out.playerLevel) &&
			h.ReadWString(out.playerLocation) &&
			h.ReadWString(out.gameDate) &&
			h.ReadWString(out.raceEditorId) &&
			h.Read(out.playerSex) &&
			h.Read(out.playerCurExp) &&
			h.Read(out.playerLvlUpExp) &&
			h.Read(out.fileTime) &&
			h.Read(out.shotWidth) &&
			h.Read(out.shotHeight);
		if (!ok) {
			a_error = std::format("header fields truncated (headerSize={})", headerSize);
			return std::nullopt;
		}
		// Version 12+ (SSE) appends the compression type.
		if (out.version >= 12) {
			h.Read(out.compressionType);
		}
		return out;
	}

	std::optional<SaveNameParts> ParseSaveName(std::string_view a_stem)
	{
		// Type+slot is the first '_' field, character ID the second.
		const auto first = a_stem.find('_');
		if (first == std::string_view::npos) {
			return std::nullopt;
		}
		const auto second = a_stem.find('_', first + 1);
		if (second == std::string_view::npos) {
			return std::nullopt;
		}

		const auto typeAndSlot = a_stem.substr(0, first);
		const auto idText = a_stem.substr(first + 1, second - first - 1);

		SaveNameParts parts;
		if (idText.size() != 8 || !ParseHex32(idText, parts.characterId)) {
			return std::nullopt;
		}

		const auto digits = typeAndSlot.find_first_of("0123456789");
		parts.saveType = std::string(typeAndSlot.substr(0, digits));
		if (digits != std::string_view::npos) {
			ParseDec32(typeAndSlot.substr(digits), parts.slot);
		}
		if (!IEquals(parts.saveType, "Save") && !IEquals(parts.saveType, "Autosave") && !IEquals(parts.saveType, "Quicksave")) {
			return std::nullopt;
		}
		return parts;
	}

	ScanResult ScanSaveDirectory(const std::filesystem::path& a_dir, unsigned a_codePage)
	{
		const auto start = std::chrono::steady_clock::now();
		ScanResult result;

		std::unordered_map<std::uint32_t, Character> byId;
		std::error_code ec;
		for (std::filesystem::directory_iterator it(a_dir, ec), end; !ec && it != end; it.increment(ec)) {
			const auto& entry = *it;
			std::error_code fileEc;
			if (!entry.is_regular_file(fileEc) || !IsEss(entry.path())) {
				continue;
			}
			++result.filesSeen;

			SaveFile save;
			save.path = entry.path();
			save.stem = text::PathUtf8(entry.path().stem());

			const auto name = ParseSaveName(save.stem);
			if (!name) {
				result.failures.push_back({ save.path, "filename has no character ID (custom save name?)" });
				continue;
			}
			save.name = *name;

			std::string error;
			const auto header = ReadSaveHeader(save.path, error, a_codePage);
			if (!header) {
				result.failures.push_back({ save.path, error });
				continue;
			}
			save.header = *header;

			auto& character = byId[save.name.characterId];
			character.characterId = save.name.characterId;
			character.saves.push_back(std::move(save));
		}
		if (ec) {
			result.failures.push_back({ a_dir, std::format("directory iteration failed: {}", ec.message()) });
		}

		for (auto& [id, character] : byId) {
			std::ranges::sort(character.saves, [](const SaveFile& a, const SaveFile& b) {
				return a.header.fileTime > b.header.fileTime;
			});
			result.characters.push_back(std::move(character));
		}
		std::ranges::sort(result.characters, [](const Character& a, const Character& b) {
			return a.Latest().header.fileTime > b.Latest().header.fileTime;
		});

		result.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		return result;
	}

	std::string FormatCharacterId(std::uint32_t a_id)
	{
		return std::format("{:08X}", a_id);
	}

	std::int64_t FileTimeToUnixSeconds(std::uint64_t a_fileTime)
	{
		constexpr std::int64_t kEpochDiff = 11644473600LL;  // 1601 -> 1970
		return static_cast<std::int64_t>(a_fileTime / 10000000ULL) - kEpochDiff;
	}

	std::string FormatFileTime(std::uint64_t a_fileTime)
	{
		if (a_fileTime == 0) {
			return "unknown";
		}
		const std::chrono::sys_seconds tp{ std::chrono::seconds{ FileTimeToUnixSeconds(a_fileTime) } };
		return std::format("{:%Y-%m-%d %H:%M:%S}", tp);
	}
}
