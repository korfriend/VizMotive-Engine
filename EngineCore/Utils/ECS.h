#pragma once

#include "JobSystem.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cstdint>
#include <cassert>
#include <atomic>
#include <memory>
#include <string>

// Entity-Component System
namespace vz::ecs
{
	// The Entity is a global unique persistent identifier within the entity-component system
	//	It can be stored and used for the duration of the application
	//	The entity can be a different value on a different run of the application, if it was serialized
	//	It must be only serialized with the SerializeEntity() function. It will ensure that entities still match with their components correctly after serialization
	using Entity = uint32_t;
	inline constexpr Entity INVALID_ENTITY = 0;
	// Runtime can create a new entity with this
	inline Entity CreateEntity()
	{
		static std::atomic<Entity> next{ INVALID_ENTITY + 1 };
		return next.fetch_add(1);
	}
}

