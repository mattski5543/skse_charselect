// Tests that an appearance export only ever saves the character being played.

#include "core/ExportTracker.h"

#include <catch2/catch_test_macros.hpp>

using namespace charselect::sidecar;
using Outcome = ExportTracker::Outcome;

namespace
{
	constexpr std::uint32_t kA = 0xAAAAAAAA;
	constexpr std::uint32_t kB = 0xBBBBBBBB;
}

TEST_CASE("Exports are only for the character being played", "[exports]")
{
	ExportTracker t;
	CHECK_FALSE(t.Current());
	CHECK_FALSE(t.Begin(kA));  // nobody yet

	t.OnSave(kA);
	CHECK(t.Current() == kA);
	REQUIRE(t.Begin(kA));
	CHECK_FALSE(t.Begin(kB));
}

TEST_CASE("A load makes earlier tickets stale (review M1)", "[exports]")
{
	// Save A in first person (the tint export fails), then load B within
	// the 3 s before the export is checked.
	ExportTracker t;
	t.OnSave(kA);
	const auto ticketA = *t.Begin(kA);
	t.OnLoad(kB);

	CHECK_FALSE(t.IsCurrent(ticketA));
	CHECK(t.Failed(ticketA) == Outcome::kStale);  // not queued for a retry...
	CHECK_FALSE(t.HasPending());
	CHECK(t.Pending().empty());                    // ...so B's face is never exported as A's
}

TEST_CASE("Loading the same character again still starts over", "[exports]")
{
	ExportTracker t;
	t.OnSave(kA);
	const auto before = *t.Begin(kA);
	t.OnLoad(kA);  // an older save of A: a different look, maybe
	CHECK_FALSE(t.IsCurrent(before));
	CHECK(t.IsCurrent(*t.Begin(kA)));
}

TEST_CASE("Saving again as the same character keeps tickets valid", "[exports]")
{
	ExportTracker t;
	t.OnLoad(kA);
	const auto ticket = *t.Begin(kA);
	t.OnSave(kA);  // quicksave right after
	CHECK(t.IsCurrent(ticket));
	CHECK(t.Failed(ticket) == Outcome::kRetry);
	t.OnSave(kA);
	CHECK(t.Pending() == std::vector{ ticket });
}

TEST_CASE("A save as a new character ID starts over (RaceMenu mid-game)", "[exports]")
{
	ExportTracker t;
	t.OnLoad(kA);
	const auto ticket = *t.Begin(kA);
	REQUIRE(t.Failed(ticket) == Outcome::kRetry);
	t.OnSave(kB);
	CHECK_FALSE(t.HasPending());
	CHECK_FALSE(t.IsCurrent(ticket));
}

TEST_CASE("The main menu and custom-named saves forget the player", "[exports]")
{
	ExportTracker t;
	t.OnSave(kA);
	const auto ticket = *t.Begin(kA);
	REQUIRE(t.Failed(ticket) == Outcome::kRetry);

	SECTION("main menu")
	{
		t.Forget();
	}
	SECTION("a save without a character ID in its name")
	{
		t.OnLoad(std::nullopt);
	}
	CHECK_FALSE(t.Current());
	CHECK_FALSE(t.HasPending());
	CHECK_FALSE(t.Begin(kA));
	CHECK(t.Failed(ticket) == Outcome::kStale);
}

TEST_CASE("Retries, then giving up", "[exports]")
{
	ExportTracker t;
	t.OnSave(kA);
	const auto ticket = *t.Begin(kA);
	for (int i = 1; i <= ExportTracker::kMaxRetries; ++i) {
		INFO("failure " << i);
		CHECK(t.Failed(ticket) == Outcome::kRetry);
		CHECK(t.Failures(kA) == i);
		CHECK(t.Pending() == std::vector{ ticket });
	}
	CHECK(t.Failed(ticket) == Outcome::kGiveUp);
	CHECK_FALSE(t.HasPending());
	CHECK(t.Failures(kA) == 0);
}

TEST_CASE("Success clears the retries", "[exports]")
{
	ExportTracker t;
	t.OnSave(kA);
	const auto ticket = *t.Begin(kA);
	REQUIRE(t.Failed(ticket) == Outcome::kRetry);
	t.Succeeded(ticket);
	CHECK_FALSE(t.HasPending());

	SECTION("a stale success changes nothing")
	{
		REQUIRE(t.Failed(ticket) == Outcome::kRetry);
		const auto stale = ticket;
		t.OnLoad(kA);
		const auto fresh = *t.Begin(kA);
		REQUIRE(t.Failed(fresh) == Outcome::kRetry);
		t.Succeeded(stale);
		CHECK(t.HasPending());
	}
}
