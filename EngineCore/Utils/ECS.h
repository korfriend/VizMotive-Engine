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

	// This is an interface class to implement a ComponentManager,
	// inherit this class if you want to work with ComponentLibrary
	class ComponentManager_Interface
	{
	public:
		virtual ~ComponentManager_Interface() = default;
		virtual void Copy(const ComponentManager_Interface& other) = 0;
		virtual void Merge(ComponentManager_Interface& other) = 0;
		virtual void Clear() = 0;
		virtual void Serialize(vz::Archive& archive, EntitySerializer& seri) = 0;
		virtual void ComponentSerialize(Entity entity, vz::Archive& archive) = 0;
		virtual void Remove(Entity entity) = 0;
		virtual void RemoveKeepSorted(Entity entity) = 0;
		virtual void MoveItem(size_t index_from, size_t index_to) = 0;
		virtual bool Contains(Entity entity) const = 0;
		virtual size_t GetIndex(Entity entity) const = 0;
		virtual size_t GetCount() const = 0;
		virtual Entity GetEntity(size_t index) const = 0;
		virtual const std::vector<Entity>& GetEntityArray() const = 0;
	};

	class ComponentLibrary;

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
			components.reserve(reservedCount);
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

		inline void Copy(const ComponentManager_Interface& other)
		{
			Copy((ComponentManager<Component>&)other);
		}

		inline void Merge(ComponentManager_Interface& other)
		{
			Merge((ComponentManager<Component>&)other);
		}

		// Read/Write everything to an archive depending on the archive state
		inline void Serialize(vz::Archive& archive, const uint64_t version)
		{
			if (archive.IsReadMode())
			{
				const size_t prev_count = components.size();

				size_t count;
				archive >> count;

				components.resize(prev_count + count);
				for (size_t i = 0; i < count; ++i)
				{
					components[prev_count + i].Serialize(archive, version);
				}

				entities.resize(prev_count + count);
				for (size_t i = 0; i < count; ++i)
				{
					Entity entity;
					SerializeEntity(archive, entity, seri);
					entities[prev_count + i] = entity;
					lookup[entity] = prev_count + i;
				}
			}
			else
			{
				archive << components.size();
				for (Component& component : components)
				{
					component.Serialize(archive, version);
				}
				for (Entity entity : entities)
				{
					SerializeEntity(archive, entity, seri);
				}
			}
		}

		//Read one single component onto an archive, make sure entity are serialized first
		inline void ComponentSerialize(const Entity entity, const uint64_t version, vz::Archive& archive)
		{
			if (archive.IsReadMode())
			{
				bool component_exists;
				archive >> component_exists;
				if (component_exists)
				{
					auto& component = this->Create(entity);
					component.Serialize(archive, version);
				}
			}
			else
			{
				auto component = this->GetComponent(entity);
				if (component != nullptr)
				{
					archive << true;
					component->Serialize(archive, version);
				}
				else
				{
					archive << false;
				}
			}
		}

		// Create a new component and retrieve a reference to it
		inline Component& Create(Entity entity)
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
			components.emplace_back(entity);

			// Also push corresponding entity 
			entities.push_back(entity);

			// Update the VUID lookup table:
			Component& new_component = components.back();
			VUID new_vuid = new_component.GetVUID();
			lookupVUID[new_vuid] = components.size();

			ComponentLibrary::AddVUID(new_vuid, static_cast<uint8_t>(new_component.GetComponentType()));

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
				vuidCompTypeMap_.erase(vuid);
				ComponentLibrary::EraseVUID(vuid);
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
				ComponentLibrary::EraseVUID(vuid);
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
			if (lookup.empty())
				return nullptr;
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
			return GetComponent(entity);
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
			return GetComponent(vuid);
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

	private:
		// This is a linear array of alive components
		std::vector<Component> components;
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
	private:
		std::unordered_map<std::string, LibraryEntry> entries_;
		inline static std::unordered_map<VUID, uint8_t> vuidCompTypeMap_;
	public:

		// Create an instance of ComponentManager of a certain data type
		//	The name must be unique, it will be used in serialization
		//	version is optional, it will be propagated to ComponentManager::Serialize() inside the EntitySerializer parameter
		template<typename T>
		inline ComponentManager<T>& Register(const std::string& name, uint64_t version = 0)
		{
			entries_[name].component_manager = std::make_unique<ComponentManager<T>>();
			entries_[name].version = version;
			return static_cast<ComponentManager<T>&>(*entries_[name].component_manager);
		}

		template<typename T>
		inline ComponentManager<T>* Get(const std::string& name)
		{
			auto it = entries_.find(name);
			if (it == entries_.end())
				return nullptr;
			return static_cast<ComponentManager<T>*>(it->second.component_manager.get());
		}

		template<typename T>
		inline const ComponentManager<T>* Get(const std::string& name) const
		{
			auto it = entries_.find(name);
			if (it == entries_.end())
				return nullptr;
			return static_cast<const ComponentManager<T>*>(it->second.component_manager.get());
		}

		inline uint64_t GetVersion(std::string name) const
		{
			auto it = entries_.find(name);
			if (it == entries_.end())
				return 0;
			return it->second.version;
		}

		// Serialize all registered component managers
		inline void Serialize(vz::Archive& archive)
		{
			if (archive.IsReadMode())
			{
				bool has_next = false;
				size_t begin = archive.GetPos();

				// First pass, gather component type versions and jump over all data:
				//	This is so that we can look up other component versions within component serialization if needed
				do
				{
					archive >> has_next;
					if (has_next)
					{
						std::string name;
						archive >> name;
						uint64_t jump_pos = 0;
						archive >> jump_pos;
						auto it = entries_.find(name);
						if (it != entries_.end())
						{
							archive >> seri.version;
							seri.library_versions[name] = seri.version;
						}
						archive.Jump(jump_pos);
					}
				} while (has_next);

				// Jump back to beginning of component library data
				archive.Jump(begin);

				// Second pass, read all component data:
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
						auto it = entries_.find(name);
						if (it != entries_.end())
						{
							archive >> seri.version;
							it->second.component_manager->Serialize(archive, seri);
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
				// Save all component type versions:
				for (auto& it : entries_)
				{
					seri.library_versions[it.first] = it.second.version;
				}
				// Serialize all component data, at this point component type version lookup is also complete
				for (auto& it : entries_)
				{
					archive << true;	// has next
					archive << it.first; // name (component type as string)
					size_t offset = archive.WriteUnknownJumpPosition(); // we will be able to jump from here...
					archive << it.second.version;
					seri.version = it.second.version;
					it.second.component_manager->Serialize(archive, seri);
					archive.PatchUnknownJumpPosition(offset); // ...to here, if this component manager was not registered
				}
				archive << false;
			}
		}

		// Serialize all components for one entity
		inline void EntitySerialize(const Entity entity, vz::Archive& archive)
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
						auto it = entries_.find(name);
						if (it != entries_.end())
						{
							archive >> seri.version;
							it->second.component_manager->Component_Serialize(entity, archive, seri);
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
				for (auto& it : entries_)
				{
					archive << true;
					archive << it.first; // name
					size_t offset = archive.WriteUnknownJumpPosition(); // we will be able to jump from here...
					archive << it.second.version;
					seri.version = it.second.version;
					it.second.component_manager->Component_Serialize(entity, archive, seri);
					archive.PatchUnknownJumpPosition(offset); // ...to here, if this component manager was not registered
				}
				archive << false;
			}
		}

		inline static uint8_t GetComponentTypeByVUID(VUID vuid) {
			auto it = vuidCompTypeMap_.find(vuid);
			if (it == vuidCompTypeMap_.end()) return 0;
			return it->second;
		}
		inline static void EraseVUID(VUID vuid) { vuidCompTypeMap_.erase(vuid); }
		inline static void AddVUID(VUID vuid, uint8_t compType) { vuidCompTypeMap_[vuid] = compType; }
	};
}

