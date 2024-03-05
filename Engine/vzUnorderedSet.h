#pragma once
// This file is used to allow replacement of std::...
#include <unordered_set>

namespace vz
{
	// when std::unordered_set is not available, then refer to another version of unordered_set datastructure
	template<typename T>
	using unordered_set = std::unordered_set<T>;
}