#pragma once

#include "JobSystem.h"
#include "Common/Archive.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>

#include <cstdint>
#include <cassert>
#include <atomic>
#include <memory>
#include <string>

#define THREAD_SAFE_ECS_COMPONENTS

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

	struct EntityMapper;
	// This is an interface class to implement a ComponentManager,
	// inherit this class if you want to work with ComponentLibrary
	class ComponentManager_Interface
	{
	public:
		virtual ~ComponentManager_Interface() = default;
#ifndef THREAD_SAFE_ECS_COMPONENTS
		virtual void Copy(const ComponentManager_Interface& other) = 0;
		virtual void Merge(ComponentManager_Interface& other) = 0;
		virtual void MoveItem(size_t index_from, size_t index_to) = 0;
#endif
		virtual void Clear() = 0;
		virtual void Serialize(vz::Archive& archive, EntityMapper& entityMapper, const uint64_t version) = 0;
		virtual void EntitySerialize(const Entity entity, const uint64_t version, vz::Archive& archive, EntityMapper& entityMapper) = 0;
		virtual void Remove(Entity entity) = 0;
		virtual void RemoveKeepSorted(Entity entity) = 0;
		virtual bool Contains(Entity entity) const = 0;
		virtual size_t GetIndex(Entity entity) const = 0;
		virtual size_t GetCount() const = 0;
		virtual Entity GetEntity(size_t index) const = 0;
		virtual const std::vector<Entity>& GetEntityArray() const = 0;

		virtual VUID GetVUID(Entity entity) const = 0;
		virtual void ResetAllRefComponents(VUID vuid) = 0;
	};

	class ComponentLibrary;
	struct EntityMapper
	{
	private:
		std::unordered_map<VUID, Entity> vuidEntityMap_;

		// non-serialized attribute
		std::unordered_map<Entity, std::vector<VUID>> entityVuidsMap_;
		std::unordered_map<Entity, Entity> remap_;

	public:
		inline void Clear() { entityVuidsMap_.clear(); vuidEntityMap_.clear(); }
		inline Entity GetEntity(VUID vuid)
		{
			auto it = vuidEntityMap_.find(vuid);
			return it == vuidEntityMap_.end() ? INVALID_ENTITY : it->second;
		}
		inline Entity RemapEntity(Entity entityArchived)
		{
			auto it = remap_.find(entityArchived);
			if (it == remap_.end())
			{
				Entity entity_new = CreateEntity();
				remap_[entityArchived] = entity_new;
				return entity_new;
			}
			return it->second;
		}
		inline void Add(VUID vuid, Entity entity)
		{
			assert(vuidEntityMap_.count(vuid) > 0);
			vuidEntityMap_[vuid] = entity;
			std::vector<VUID>& vuids = entityVuidsMap_[entity];
			vuids.push_back(vuid);
		}
		inline void Serialize(vz::Archive& archive)
		{
			if (archive.IsReadMode())
			{
				std::string seri_name;
				archive >> seri_name;
				assert(seri_name == "EntityMapper");
				Clear();
				size_t element_count;
				archive >> element_count;
				std::vector<uint64_t> data;
				archive >> data;
				vuidEntityMap_.reserve(element_count);
				for (size_t i = 0; i < element_count; ++i)
				{
					Add(data[2 * i + 0], static_cast<Entity>(data[2 * i + 1]));
				}
			}
			else
			{
				archive << "EntityMapper";
				size_t element_count = vuidEntityMap_.size();
				archive << element_count;

				std::vector<uint64_t> data(element_count * 2);
				size_t count = 0;
				for (auto& it : vuidEntityMap_)
				{
					data[2 * count + 0] = it.first;
					data[2 * count + 1] = it.second;
				}
				archive << data;
			}
		}
	};

	// The ComponentManager is a container that stores components and matches them with entities
	//	Note: final keyword is used to indicate this is a final implementation.
	//	This allows function inlining and avoid calls, improves performance considerably
	template<typename Component>
	class ComponentManager final : public ComponentManager_Interface
	{
	public:

		// reservedCount : how much components can be held initially before growing the container
		ComponentManager(size_t reservedCount = 0)
		{
#ifndef THREAD_SAFE_ECS_COMPONENTS
			components.reserve(reservedCount);
#endif
			entities.reserve(reservedCount);
			lookup.reserve(reservedCount);
			lookupVUID.reserve(reservedCount);
		}

		// Clear the whole container
		inline void Clear()
		{
			components.clear();
			entities.clear();
			lookup.clear();
			lookupVUID.clear();
		}

#ifndef THREAD_SAFE_ECS_COMPONENTS
		// Perform deep copy of all the contents of "other" into this
		inline void Copy(const ComponentManager<Component>& other)
		{
			components.reserve(GetCount() + other.GetCount());
			entities.reserve(GetCount() + other.GetCount());
			lookup.reserve(GetCount() + other.GetCount());
			lookupVUID.reserve(GetCount() + other.GetCount());
			for (size_t i = 0; i < other.GetCount(); ++i)
			{
				Entity entity = other.entities[i];
				VUID vuid = other.components[i].GetVUID();
				assert(!Contains(entity));
				entities.push_back(entity);
				lookup[entity] = components.size();
				lookupVUID[vuid] = components.size();
				components.push_back(other.components[i]);
			}
		}

		// Merge in an other component manager of the same type to this.
		//	The other component manager MUST NOT contain any of the same entities!
		//	The other component manager is not retained after this operation!
		inline void Merge(ComponentManager<Component>& other)
		{
			components.reserve(GetCount() + other.GetCount());
			entities.reserve(GetCount() + other.GetCount());
			lookup.reserve(GetCount() + other.GetCount());
			lookupVUID.reserve(GetCount() + other.GetCount());
		
			for (size_t i = 0; i < other.GetCount(); ++i)
			{
				Entity entity = other.entities[i];
				VUID vuid = other.components[i].GetVUID();
				assert(!Contains(entity));
				assert(!ContainsVUID(vuid));
				entities.push_back(entity);
				lookup[entity] = components.size();
				lookupVUID[vuid] = components.size();
				components.push_back(std::move(other.components[i]));
			}
		
			other.Clear();
		}
#endif

		inline void Copy(const ComponentManager_Interface& other)
		{
			Copy((ComponentManager<Component>&)other);
		}

		inline void Merge(ComponentManager_Interface& other)
		{
			Merge((ComponentManager<Component>&)other);
		}

		// Read/Write everything to an archive depending on the archive state
		inline void Serialize(vz::Archive& archive, EntityMapper& entityMapper, const uint64_t version)
		{
			if (archive.IsReadMode())
			{
				// component meta info
				std::string origin_serializer;
				archive >> origin_serializer;
				assert(origin_serializer == "ComponentManager");
				uint8_t u8_data;
				archive >> u8_data;
				//assert(Component::IntrinsicType == static_cast<ComponentType>(u8_data));

				size_t count; // # of component managers
				archive >> count;
				// for each component manager
				for (size_t i = 0; i < count; ++i)
				{
					VUID vuid;
					archive >> vuid;
					auto it = lookupVUID.find(vuid);
					if (it != lookupVUID.end())
					{
						// replace the previous component with the archive one (that has the same VUID).
						// important note: in this case, we use the component entity as is.
						size_t index_comp = it->second;
						components[index_comp].Serialize(archive, version);
					}
					else
					{
						// add new component that does not belong to the current component manager
						assert(!ContainsVUID(vuid));
						Entity entity_archived = entityMapper.GetEntity(vuid);
						Entity entity = entityMapper.RemapEntity(entity_archived);
						Create(entity, vuid);
						components[lookup[entity]].Serialize(archive, version);
					}
				}
			}
			else
			{
				// component meta info
				archive << "ComponentManager";
				archive << static_cast<uint8_t>(Component::IntrinsicType);

				archive << components.size();
				for (Component& component : components)
				{
					VUID vuid = component.GetVUID();
					archive << vuid;
					component.Serialize(archive, version);

					entityMapper.Add(vuid, component.GetEntity());
				}
			}
		}

		// Read/write one single component onto an archive, make sure entity are serialized first
		inline void EntitySerialize(const Entity entity, const uint64_t version, vz::Archive& archive, EntityMapper& entityMapper)
		{
			if (archive.IsReadMode())
			{
				bool component_exists;
				archive >> component_exists;
				if (component_exists)
				{
					VUID vuid;
					archive >> vuid;
					auto it = lookupVUID.find(vuid);
					if (it != lookupVUID.end())
					{
						// replace the previous component with the archive one (that has the same VUID).
						// important note: in this case, we use the component entity as is.
						size_t index_comp = it->second;
						components[index_comp].Serialize(archive, version);
					}
					else
					{
						// add new component that does not belong to the current component manager
						assert(!ContainsVUID(vuid));
						Entity entity_archived = entityMapper.GetEntity(vuid);
						Entity entity = entityMapper.RemapEntity(entity_archived);
						Create(entity, vuid);
						components[lookup[entity]].Serialize(archive, version);
					}
				}
			}
			else
			{
				auto component = this->GetComponent(entity);
				if (component != nullptr)
				{
					archive << true;

					VUID vuid = component->GetVUID();
					archive << vuid;
					component->Serialize(archive, version);

					entityMapper.Add(vuid, component->GetEntity());
				}
				else
				{
					archive << false;
				}
			}
		}

		// Create a new component and retrieve a reference to it
		inline Component& Create(Entity entity, VUID vuid = 0)
		{
			// INVALID_ENTITY is not allowed!
			assert(entity != INVALID_ENTITY);

			// Only one of this component type per entity is allowed!
			assert(lookup.find(entity) == lookup.end());

			// Entity count must always be the same as the number of coponents!
			assert(entities.size() == components.size());
			assert(lookup.size() == components.size());

			// Update the entity lookup table:
			lookup[entity] = components.size();

			// New components are always pushed to the end:
			components.emplace_back(entity, vuid);

			// Also push corresponding entity 
			entities.push_back(entity);

			// Update the VUID lookup table:
			Component& new_component = components.back();
			VUID new_vuid = new_component.GetVUID();
			lookupVUID[new_vuid] = components.size() - 1; // note new_component is from the new-counted components

			return new_component;
		}

		// Remove a component of a certain entity if it exists
		inline void Remove(Entity entity)
		{
			auto it = lookup.find(entity);
			if (it != lookup.end())
			{
				// Directly index into components and entities array:
				const size_t index = it->second;
				assert(entities[index] == entity);

				VUID vuid = components[index].GetVUID();

				if (index < components.size() - 1)
				{
					// Swap out the dead element with the last one:
					components[index] = std::move(components.back()); // try to use move instead of copy
					entities[index] = entities.back();

					// Update the lookup tables:
					lookup[entities[index]] = index;

					VUID vuid_updated = components[index].GetVUID();
					lookupVUID[vuid_updated] = index;
				}

				// Shrink the container:
				components.pop_back();
				entities.pop_back();
				lookup.erase(entity);
				lookupVUID.erase(vuid);
			}
		}

		// Remove a component of a certain entity if it exists while keeping the current ordering
		inline void RemoveKeepSorted(Entity entity)
		{
			auto it = lookup.find(entity);
			if (it != lookup.end())
			{
				// Directly index into components and entities array:
				const size_t index = it->second;
				assert(entities[index] == entity);
				VUID vuid = components[index].GetVUID();

				if (index < components.size() - 1)
				{
					// Move every component left by one that is after this element:
					for (size_t i = index + 1; i < components.size(); ++i)
					{
						components[i - 1] = std::move(components[i]);
					}
					// Move every entity left by one that is after this element and update lut:
					for (size_t i = index + 1; i < entities.size(); ++i)
					{
						entities[i - 1] = entities[i];
						lookup[entities[i - 1]] = i - 1;

						VUID vuid_updated = components[i - 1].GetVUID();
						lookupVUID[vuid_updated] = i - 1;
					}
				}

				// Shrink the container:
				components.pop_back();
				entities.pop_back();
				lookup.erase(entity);
				lookupVUID.erase(vuid);
			}
		}

		// Place an entity-component to the specified index position while keeping the ordering intact
		inline void MoveItem(size_t index_from, size_t index_to)
		{
			assert(index_from < GetCount());
			assert(index_to < GetCount());
			if (index_from == index_to)
			{
				return;
			}

			// Save the moved component and entity:
			Component component = std::move(components[index_from]);
			Entity entity = entities[index_from];
			VUID vuid_from = component.GetVUID();

			// Every other entity-component that's in the way gets moved by one and lut is kept updated:
			const int direction = index_from < index_to ? 1 : -1;
			for (size_t i = index_from; i != index_to; i += direction)
			{
				const size_t next = i + direction;
				components[i] = std::move(components[next]);
				entities[i] = entities[next];
				lookup[entities[i]] = i;

				VUID vuid = components[i].GetVUID();
				lookupVUID[vuid] = i;
			}

			// Saved entity-component moved to the required position:
			components[index_to] = std::move(component);
			entities[index_to] = entity;
			lookup[entity] = index_to;
			lookupVUID[vuid_from] = index_to;
		}

		// Check if a component exists for a given entity or not
		inline bool Contains(Entity entity) const
		{
			if (lookup.empty())
				return false;
			return lookup.find(entity) != lookup.end();
		}
		// Check if a component exists for a given VUID or not
		inline bool ContainsVUID(VUID vuid) const
		{
			if (lookupVUID.empty())
				return false;
			return lookupVUID.find(vuid) != lookupVUID.end();
		}

		// Retrieve a [read/write] component specified by an entity (if it exists, otherwise nullptr)
		inline Component* GetComponent(Entity entity)
		{
			if (lookup.empty()) {
				return nullptr;
			}
			auto it = lookup.find(entity);
			if (it != lookup.end())
			{
				return &components[it->second];
			}
			return nullptr;
		}

		// Retrieve a [read only] component specified by an entity (if it exists, otherwise nullptr)
		inline const Component* GetComponent(Entity entity) const
		{
			if (lookup.empty()) {
				return nullptr;
			}
			const auto it = lookup.find(entity);
			if (it != lookup.end())
			{
				return &components[it->second];
			}
			return nullptr;
		}

		// Retrieve a [read/write] component specified by a VUID (if it exists, otherwise nullptr)
		inline Component* GetComponent(VUID vuid)
		{
			if (lookupVUID.empty())
				return nullptr;
			auto it = lookupVUID.find(vuid);
			if (it != lookupVUID.end())
			{
				return &components[it->second];
			}
			return nullptr;
		}

		// Retrieve a [read only] component specified by an entity (if it exists, otherwise nullptr)
		inline const Component* GetComponent(VUID vuid) const
		{
			if (lookupVUID.empty())
				return nullptr;
			const auto it = lookupVUID.find(vuid);
			if (it != lookupVUID.end())
			{
				return &components[it->second];
			}
			return nullptr;
		}

		// Retrieve component index by entity handle (if not exists, returns ~0ull value)
		inline size_t GetIndex(Entity entity) const
		{
			if (lookup.empty())
				return ~0ull;
			const auto it = lookup.find(entity);
			if (it != lookup.end())
			{
				return it->second;
			}
			return ~0ull;
		}

		// Retrieve component index by entity handle (if not exists, returns ~0ull value)
		inline size_t GetIndex(VUID vuid) const
		{
			if (lookupVUID.empty())
				return ~0ull;
			const auto it = lookupVUID.find(vuid);
			if (it != lookupVUID.end())
			{
				return it->second;
			}
			return ~0ull;
		}

		// Retrieve the number of existing entries
		inline size_t GetCount() const { return components.size(); }

		// Directly index a specific component without indirection
		//	0 <= index < GetCount()
		inline Entity GetEntity(size_t index) const { return entities[index]; }

		// Directly index a specific [read/write] component without indirection
		//	0 <= index < GetCount()
		inline Component& operator[](size_t index) { return components[index]; }

		// Directly index a specific [read only] component without indirection
		//	0 <= index < GetCount()
		inline const Component& operator[](size_t index) const { return components[index]; }

		// Returns the tightly packed [read only] entity array
		inline const std::vector<Entity>& GetEntityArray() const { return entities; }

		// Returns the tightly packed [read only] component array
		inline const std::vector<Component>& GetComponentArray() const { return components; }

		inline VUID GetVUID(Entity entity) const {
			const Component* comp = GetComponent(entity);
			if (comp == nullptr)
				return INVALID_VUID;
			return comp == nullptr ? INVALID_VUID : comp->GetVUID();
		}

		inline void ResetAllRefComponents(VUID vuid) override {
			for (Component& comp : components) {
				comp.ResetRefComponents(vuid);
			}
		}

	private:
		// This is a linear array of alive components
#ifdef THREAD_SAFE_ECS_COMPONENTS
		std::deque<Component> components;
#else
		std::vector<Component> components;
#endif
		// This is a linear array of entities corresponding to each alive component
		std::vector<Entity> entities;
		// This is a lookup table for entities
		std::unordered_map<Entity, size_t> lookup;
		// This is a lookup table for component's vuid
		std::unordered_map<VUID, size_t> lookupVUID;

		// Disallow this to be copied by mistake
		ComponentManager(const ComponentManager&) = delete;
	};

	// This is the class to store all component managers,
	// this is useful for bulk operation of all attached components within an entity
	class ComponentLibrary
	{
	public:
		struct LibraryEntry
		{
			std::unique_ptr<ComponentManager_Interface> component_manager;
			uint64_t version = 0;
		};

		std::unordered_map<std::string, LibraryEntry> entries;

	public:

		// Create an instance of ComponentManager of a certain data type
		//	The name must be unique, it will be used in serialization
		//	version is optional, it will be propagated to ComponentManager::Serialize() 
		template<typename T>
		inline ComponentManager<T>& Register(const std::string& name, uint64_t version = 0)
		{
			entries[name].component_manager = std::make_unique<ComponentManager<T>>();
			entries[name].version = version;
			return static_cast<ComponentManager<T>&>(*entries[name].component_manager);
		}

		template<typename T>
		inline ComponentManager<T>* Get(const std::string& name)
		{
			auto it = entries.find(name);
			if (it == entries.end())
				return nullptr;
			return static_cast<ComponentManager<T>*>(it->second.component_manager.get());
		}

		template<typename T>
		inline const ComponentManager<T>* Get(const std::string& name) const
		{
			auto it = entries.find(name);
			if (it == entries.end())
				return nullptr;
			return static_cast<const ComponentManager<T>*>(it->second.component_manager.get());
		}

		inline uint64_t GetVersion(std::string name) const
		{
			auto it = entries.find(name);
			if (it == entries.end())
				return 0;
			return it->second.version;
		}

		// Serialize all registered component managers
		inline void Serialize(vz::Archive& archive, EntityMapper& entityMapper)
		{
			if (archive.IsReadMode())
			{
				bool has_next = false;
				// Read all component data:
				//	At this point, all existing component type versions are available
				do
				{
					archive >> has_next;
					if (has_next)
					{
						std::string name;
						archive >> name;
						uint64_t jump_pos = 0;
						archive >> jump_pos;
						auto it = entries.find(name);
						if (it != entries.end())
						{
							uint64_t version;
							archive >> version;
							it->second.component_manager->Serialize(archive, entityMapper, version);
						}
						else
						{
							// component manager of this name was not registered, skip serialization by jumping over the data
							archive.Jump(jump_pos);
						}
					}
				} while (has_next);
			}
			else
			{
				// Serialize all component data, at this point component type version lookup is also complete
				for (auto& it : entries)
				{
					archive << true;	// has next
					archive << it.first; // name (component type as string)
					size_t offset = archive.WriteUnknownJumpPosition(); // we will be able to jump from here...
					archive << it.second.version;
					it.second.component_manager->Serialize(archive, entityMapper, it.second.version);
					archive.PatchUnknownJumpPosition(offset); // ...to here, if this component manager was not registered
				}
				archive << false;
			}
		}

		// Serialize all components for one entity
		inline void EntitySerialize(const Entity entity, vz::Archive& archive, EntityMapper& entityMapper)
		{
			if (archive.IsReadMode())
			{
				bool has_next = false;
				do
				{
					archive >> has_next;
					if (has_next)
					{
						std::string name;
						archive >> name;
						uint64_t jump_size = 0;
						archive >> jump_size;
						auto it = entries.find(name);
						if (it != entries.end())
						{
							uint64_t version;
							archive >> version;
							it->second.component_manager->EntitySerialize(entity, version, archive, entityMapper);
						}
						else
						{
							// component manager of this name was not registered, skip serialization by jumping over the data
							archive.Jump(jump_size);
						}
					}
				} while (has_next);
			}
			else
			{
				for (auto& it : entries)
				{
					archive << true;
					archive << it.first; // name
					size_t offset = archive.WriteUnknownJumpPosition(); // we will be able to jump from here...
					archive << it.second.version;
					it.second.component_manager->EntitySerialize(entity, it.second.version, archive, entityMapper);
					archive.PatchUnknownJumpPosition(offset); // ...to here, if this component manager was not registered
				}
				archive << false;
			}
		}
	};
}

