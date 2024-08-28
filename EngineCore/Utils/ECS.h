#pragma once

#include "JobSystem.h"
#include "Common/Archive.h"

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

	class ComponentLibrary;
	struct EntitySerializer
	{
		vz::jobsystem::context ctx; // allow components to spawn serialization subtasks
		std::unordered_map<uint64_t, Entity> remap;
		bool allow_remap = true;
		uint64_t version = 0; // The ComponentLibrary serialization will modify this by the registered component's version number
		std::unordered_set<std::string> resource_registration; // register for resource manager serialization
		ComponentLibrary* componentlibrary = nullptr;
		std::unordered_map<std::string, uint64_t> library_versions;

		~EntitySerializer()
		{
			vz::jobsystem::Wait(ctx); // automatically wait for all subtasks after serialization
		}

		// Returns the library version of the currently serializing Component
		//	If not using ComponentLibrary, it returns version set by the user.
		uint64_t GetVersion() const
		{
			return version;
		}
		uint64_t GetVersion(const std::string& name) const
		{
			auto it = library_versions.find(name);
			if (it != library_versions.end())
			{
				return it->second;
			}
			return 0;
		}

		void RegisterResource(const std::string& resource_name)
		{
			if (resource_name.empty())
				return;
			resource_registration.insert(resource_name);
		}
	};
	// This is the safe way to serialize an entity
	inline void SerializeEntity(vz::Archive& archive, Entity& entity, EntitySerializer& seri)
	{
		if (archive.IsReadMode())
		{
			// Entities are always serialized as uint64_t for back-compat
			uint64_t mem;
			archive >> mem;

			if (mem != INVALID_ENTITY && seri.allow_remap)
			{
				auto it = seri.remap.find(mem);
				if (it == seri.remap.end())
				{
					entity = CreateEntity();
					seri.remap[mem] = entity;
				}
				else
				{
					entity = it->second;
				}
			}
			else
			{
				entity = (Entity)mem;
			}
		}
		else
		{
#if defined(__GNUC__) && !defined(__SCE__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif // __GNUC__ && !__SCE__

			archive << entity;

#if defined(__GNUC__) && !defined(__SCE__)
#pragma GCC diagnostic pop
#endif // __GNUC__ && !__SCE__
		}
	}
}

