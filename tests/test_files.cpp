// Reading, moving and growing what CharSelect keeps: core/Files.h and
// core/FreshArray.h.

#include "TestUtil.h"
#include "core/Files.h"
#include "core/FreshArray.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <share.h>

using namespace charselect;

namespace
{
	// Holds a file open with no sharing, as an editor or an antivirus can.
	class LockedFile
	{
	public:
		explicit LockedFile(const std::filesystem::path& a_path) :
			_file(_wfsopen(a_path.c_str(), L"rb", _SH_DENYRW)) {}
		~LockedFile()
		{
			if (_file) {
				std::fclose(_file);
			}
		}
		LockedFile(const LockedFile&) = delete;
		LockedFile& operator=(const LockedFile&) = delete;

		[[nodiscard]] bool Held() const { return _file != nullptr; }

	private:
		std::FILE* _file;
	};
}

TEST_CASE("ReadForUpdate tells a missing file from one it can't read", "[files]")
{
	test::TempDir dir;
	const auto    path = dir / "Layout.ini";

	SECTION("no file: nothing in it yet")
	{
		const auto text = files::ReadForUpdate(path);
		REQUIRE(text.has_value());
		CHECK(text->empty());
	}
	SECTION("a file: what's in it")
	{
		test::WriteText(path, "[cell]\nslot1x = 1\n");
		CHECK(files::ReadForUpdate(path) == "[cell]\nslot1x = 1\n");
	}
	SECTION("a file another program holds locked: nothing, so it isn't written over")
	{
		test::WriteText(path, "[cell]\nslot1x = 1\n");
		const LockedFile lock(path);
		REQUIRE(lock.Held());
		CHECK_FALSE(files::ReadForUpdate(path).has_value());
	}
	SECTION("a folder where the file should be")
	{
		std::filesystem::create_directories(path);
		CHECK_FALSE(files::ReadForUpdate(path).has_value());
	}
}

TEST_CASE("MoveReplacing moves a file over the old one in one step", "[files]")
{
	test::TempDir dir;
	const auto    from = dir / "Presets" / "CharSelect_00000001.jslot";
	const auto    to = dir / "Exported" / "CharSelect_00000001.jslot";
	test::WriteText(from, "new");
	std::string error;

	SECTION("into a folder that doesn't exist yet")
	{
		REQUIRE(files::MoveReplacing(from, to, error));
		CHECK(test::ReadText(to) == "new");
		CHECK_FALSE(std::filesystem::exists(from));
	}
	SECTION("replacing the previous file")
	{
		test::WriteText(to, "old, and longer");
		REQUIRE(files::MoveReplacing(from, to, error));
		CHECK(test::ReadText(to) == "new");
		CHECK_FALSE(std::filesystem::exists(from));
	}
	SECTION("nothing to move: the previous file stays")
	{
		test::WriteText(to, "old");
		std::filesystem::remove(from);
		CHECK_FALSE(files::MoveReplacing(from, to, error));
		CHECK_FALSE(error.empty());
		CHECK(test::ReadText(to) == "old");
	}
	SECTION("the previous file locked: it stays, and so does the new one")
	{
		test::WriteText(to, "old");
		{
			const LockedFile lock(to);
			REQUIRE(lock.Held());
			CHECK_FALSE(files::MoveReplacing(from, to, error));
			CHECK_FALSE(error.empty());
		}
		CHECK(test::ReadText(to) == "old");
		CHECK(test::ReadText(from) == "new");
	}
}

TEST_CASE("MoveMatching moves only CharSelect's presets", "[files]")
{
	test::TempDir dir;
	const auto    presets = dir / "Presets";
	const auto    exported = dir / "Exported";
	test::WriteText(presets / "CharSelect_00000001.jslot", "one");
	test::WriteText(presets / "charselect_00000002.JSLOT", "two");
	test::WriteText(presets / "My character.jslot", "the player's own");
	test::WriteText(presets / "CharSelect_00000003.dds", "not a preset");
	std::filesystem::create_directories(presets / "CharSelect_folder.jslot");
	test::WriteText(exported / "CharSelect_00000001.jslot", "older");

	const auto result = files::MoveMatching(presets, exported, "CharSelect_", ".jslot");

	CHECK(result.moved == 2);
	CHECK(result.failed.empty());
	CHECK(test::ReadText(exported / "CharSelect_00000001.jslot") == "one");
	CHECK(test::ReadText(exported / "charselect_00000002.JSLOT") == "two");
	CHECK(test::ReadText(presets / "My character.jslot") == "the player's own");
	CHECK(std::filesystem::exists(presets / "CharSelect_00000003.dds"));
	CHECK(std::filesystem::is_directory(presets / "CharSelect_folder.jslot"));
	CHECK_FALSE(std::filesystem::exists(presets / "CharSelect_00000001.jslot"));
}

TEST_CASE("MoveMatching with no folder moves nothing", "[files]")
{
	test::TempDir dir;
	const auto    result = files::MoveMatching(dir / "Presets", dir / "Exported", "CharSelect_", ".jslot");
	CHECK(result.moved == 0);
	CHECK(result.failed.empty());
	CHECK_FALSE(std::filesystem::exists(dir / "Exported"));
}

namespace
{
	// Hands out zeroed arrays and keeps them, so the test can check the old
	// ones were left as they were.
	template <class E>
	struct Heap
	{
		std::vector<std::unique_ptr<E[]>> blocks;
		std::vector<std::size_t>          sizes;

		E* operator()(std::size_t a_n)
		{
			blocks.push_back(std::make_unique<E[]>(a_n));
			sizes.push_back(a_n);
			return blocks.back().get();
		}
	};

	struct PerkRank
	{
		const void*  perk{ nullptr };
		std::uint8_t rank{ 0 };

		bool operator==(const PerkRank&) const = default;
	};
}

TEST_CASE("AppendFresh grows a list in a new array and leaves the old one as it was", "[files][resetguard]")
{
	int        a{}, b{}, c{};
	Heap<int*> heap;

	SECTION("a list with items")
	{
		int*          old[] = { &a, &b };
		int**         list = old;
		std::uint32_t count = 2;
		arrays::AppendFresh(list, count, std::vector<int*>{ &c }, heap);

		REQUIRE(count == 3);
		CHECK(list != old);
		CHECK(list[0] == &a);
		CHECK(list[1] == &b);
		CHECK(list[2] == &c);
		CHECK(old[0] == &a);  // a reader still holding the old array sees what it saw
		CHECK(old[1] == &b);
		CHECK(heap.sizes == std::vector<std::size_t>{ 3 });
	}
	SECTION("an empty list")
	{
		int**         list = nullptr;
		std::uint32_t count = 0;
		arrays::AppendFresh(list, count, std::vector<int*>{ &a, &b }, heap);

		REQUIRE(count == 2);
		CHECK(list[0] == &a);
		CHECK(list[1] == &b);
	}
	SECTION("a list of records, like perks and their ranks")
	{
		Heap<PerkRank> perks;
		PerkRank       old[] = { { &a, 1 } };
		PerkRank*      list = old;
		std::uint32_t  count = 1;
		arrays::AppendFresh(list, count, std::vector<PerkRank>{ { &b, 2 }, { &c, 3 } }, perks);

		REQUIRE(count == 3);
		CHECK(list[0] == PerkRank{ &a, 1 });
		CHECK(list[1] == PerkRank{ &b, 2 });
		CHECK(list[2] == PerkRank{ &c, 3 });
		CHECK(old[0] == PerkRank{ &a, 1 });
	}
}
