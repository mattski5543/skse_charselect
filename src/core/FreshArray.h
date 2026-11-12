#pragma once

// Growing an engine array (a pointer and a count kept side by side, as in
// game records' keyword, spell and perk lists) without freeing the old one.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace charselect::arrays
{
	// Appends a_add to a_array and a_count, in a fresh array from a_alloc(n)
	// (n elements, zeroed). The old array is never freed or written: another
	// thread may be reading it (a script checking a keyword, an actor's 3D
	// being built), or, on a record the game emptied, it may already be
	// freed. It stays allocated, a few bytes per record. The new array is in
	// place before the count grows, so a reader never counts past the end of
	// the array it holds.
	template <class E, class N, class Alloc>
	void AppendFresh(E*& a_array, N& a_count, const std::vector<E>& a_add, Alloc&& a_alloc)
	{
		const auto count = static_cast<std::size_t>(a_count);
		E*         out = a_alloc(count + a_add.size());
		if (a_array) {
			std::copy_n(a_array, count, out);
		}
		std::ranges::copy(a_add, out + count);
		a_array = out;
		std::atomic_thread_fence(std::memory_order_release);
		a_count = static_cast<N>(count + a_add.size());
	}
}
