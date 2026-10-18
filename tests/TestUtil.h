#pragma once

// Helpers shared by the tests: a temporary folder and a writer for
// synthetic Skyrim save headers.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace test
{
	// A fresh folder under the system temp folder, deleted with everything in
	// it when the object goes away.
	class TempDir
	{
	public:
		TempDir()
		{
			static std::atomic<int> counter{ 0 };
			const auto              stamp = std::chrono::steady_clock::now().time_since_epoch().count();
			_path = std::filesystem::temp_directory_path() / ("charselect_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
			std::filesystem::create_directories(_path);
		}
		~TempDir()
		{
			std::error_code ec;
			std::filesystem::remove_all(_path, ec);
		}
		TempDir(const TempDir&) = delete;
		TempDir& operator=(const TempDir&) = delete;

		[[nodiscard]] const std::filesystem::path& Path() const { return _path; }
		[[nodiscard]] std::filesystem::path        operator/(std::string_view a_name) const { return _path / a_name; }
		[[nodiscard]] std::filesystem::path        operator/(std::wstring_view a_name) const { return _path / a_name; }

	private:
		std::filesystem::path _path;
	};

	inline std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
	}

	inline std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream in(a_path, std::ios::binary);
		return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
	}

	inline void WriteText(const std::filesystem::path& a_path, std::string_view a_text)
	{
		std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
		out.write(a_text.data(), static_cast<std::streamsize>(a_text.size()));
	}

	template <class T>
	T ReadAt(const std::vector<std::uint8_t>& a_bytes, std::size_t a_offset)
	{
		T value{};
		std::memcpy(&value, a_bytes.data() + a_offset, sizeof(T));
		return value;
	}

	// The fields of an .ess header, in file order (UESP "Save File Format").
	// Strings are raw bytes (Windows-1252 in real saves).
	struct EssHeader
	{
		std::uint32_t version{ 12 };
		std::uint32_t saveNumber{ 7 };
		std::string   playerName{ "Sven" };
		std::uint32_t playerLevel{ 66 };
		std::string   playerLocation{ "Whiterun" };
		std::string   gameDate{ "4E 201, Morndas, 8:00 AM" };
		std::string   raceEditorId{ "NordRace" };
		std::uint16_t playerSex{ 0 };
		float         playerCurExp{ 12.5f };
		float         playerLvlUpExp{ 300.0f };
		std::uint64_t fileTime{ 134048056950000000ULL };
		std::uint32_t shotWidth{ 320 };
		std::uint32_t shotHeight{ 192 };
		std::uint16_t compressionType{ 2 };
	};

	class ByteWriter
	{
	public:
		template <class T>
		ByteWriter& Put(T a_value)
		{
			const auto* p = reinterpret_cast<const std::uint8_t*>(&a_value);
			bytes.insert(bytes.end(), p, p + sizeof(T));
			return *this;
		}
		ByteWriter& PutWString(std::string_view a_text)
		{
			Put(static_cast<std::uint16_t>(a_text.size()));
			bytes.insert(bytes.end(), a_text.begin(), a_text.end());
			return *this;
		}
		ByteWriter& PutRaw(std::string_view a_text)
		{
			bytes.insert(bytes.end(), a_text.begin(), a_text.end());
			return *this;
		}

		std::vector<std::uint8_t> bytes;
	};

	// The header block only (what headerSize counts).
	inline std::vector<std::uint8_t> EssHeaderBytes(const EssHeader& a_h)
	{
		ByteWriter w;
		w.Put(a_h.version).Put(a_h.saveNumber).PutWString(a_h.playerName).Put(a_h.playerLevel).PutWString(a_h.playerLocation);
		w.PutWString(a_h.gameDate).PutWString(a_h.raceEditorId).Put(a_h.playerSex).Put(a_h.playerCurExp).Put(a_h.playerLvlUpExp);
		w.Put(a_h.fileTime).Put(a_h.shotWidth).Put(a_h.shotHeight);
		if (a_h.version >= 12) {
			w.Put(a_h.compressionType);
		}
		return w.bytes;
	}

	// A whole file: magic, header size, header, then some filler standing in
	// for the screenshot and game data. a_headerSize overrides the real size.
	inline void WriteEss(const std::filesystem::path& a_path, const EssHeader& a_h, std::optional<std::uint32_t> a_headerSize = std::nullopt,
		std::size_t a_filler = 4096)
	{
		const auto header = EssHeaderBytes(a_h);
		ByteWriter w;
		w.PutRaw("TESV_SAVEGAME").Put(a_headerSize.value_or(static_cast<std::uint32_t>(header.size())));
		w.bytes.insert(w.bytes.end(), header.begin(), header.end());
		w.bytes.resize(w.bytes.size() + a_filler, 0xAB);
		std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
		out.write(reinterpret_cast<const char*>(w.bytes.data()), static_cast<std::streamsize>(w.bytes.size()));
	}

	// FILETIME ticks (100 ns since 1601-01-01) for a wall-clock time.
	inline std::uint64_t FileTime(int a_year, unsigned a_month, unsigned a_day, int a_hour = 0, int a_min = 0, int a_sec = 0)
	{
		using namespace std::chrono;
		const auto day = sys_days{ year{ a_year } / month{ a_month } / a_day };
		const auto secs = duration_cast<seconds>(day.time_since_epoch()).count() + a_hour * 3600LL + a_min * 60LL + a_sec;
		return static_cast<std::uint64_t>(secs + 11644473600LL) * 10000000ULL;
	}
}
