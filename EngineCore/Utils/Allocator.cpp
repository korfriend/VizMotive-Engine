#include "Allocator.h"

#include <cassert>
#include <atomic>

namespace vz::allocator
{
	// Single instance in Engine.dll — all DLLs register into and query from this same array
	static SharedBlockAllocator* block_allocators[256] = {};
	static std::atomic<uint8_t> next_allocator_id{ 0 };

	uint8_t register_shared_block_allocator(SharedBlockAllocator* allocator)
	{
		uint8_t id = next_allocator_id.fetch_add(1);
		assert(id < 256);
		block_allocators[id] = allocator;
		return id;
	}

	uint8_t get_shared_block_allocator_count()
	{
		return next_allocator_id.load();
	}

	SharedBlockAllocator* get_shared_block_allocator(uint8_t id)
	{
		return block_allocators[id];
	}
}
