#pragma once
// This file is used to allow replacement of std::...
#include <unordered_map>

namespace vz
{
	// when std::unordered_map is not available, then refer to another version of unordered_map datastructure
	template<typename K, typename V, typename H = std::hash<K>, typename E = std::equal_to<K>, typename A = std::allocator<std::pair<const K, V> > >
	using unordered_map = std::unordered_map<K, V, H, E, A>;
}
