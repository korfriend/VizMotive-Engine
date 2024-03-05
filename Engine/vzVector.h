#pragma once
// This file is used to allow replacement of std::...
#include <vector>

namespace vz
{
	// when std::vector is not available, then refer to another version of vector datastructure

	template<typename T, typename A = std::allocator<T>>
	using vector = std::vector<T, A>;
}

