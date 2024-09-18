#include "Components.h"
#include "Utils/Backlog.h"
#include "Utils/ECS.h"
#include "Common/Archive.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <memory>
#include <unordered_map>

namespace vz
{
	static std::unordered_map<Entity, std::unique_ptr<Scene>> scenes_;

	Scene* GetScene(const Entity entity) {
		auto it = scenes_.find(entity);
		return it != scenes_.end() ? it->second.get() : nullptr;
	}
	Scene* GetFirstSceneByName(const std::string& name) {
		for (auto& it : scenes_) {
			if (it.second->GetName() == name) return it.second.get();
		}
		return nullptr;
	}
	Scene* CreateScene(const std::string& name, const Entity entity)
	{
		Entity ett = entity;
		if (entity == 0)
		{
			ett = ecs::CreateEntity();
		}

		scenes_[ett] = std::make_unique<Scene>(ett, name);
		return scenes_[ett].get();
	}


	void Scene::AddEntity(const Entity entity)
	{
		if (compfactory::ContainRenderableComponent(entity))
		{
			lookupRenderables_[entity] = renderables_.size();
			renderables_.push_back(entity);
		}
		if (compfactory::ContainLightComponent(entity))
		{
			lookupLights_[entity] = renderables_.size();
			lights_.push_back(entity);
		}
	}
	
	void AddEntities(const std::vector<Entity>& entities);

	void Remove(const Entity entity);

	void RemoveEntities(const std::vector<Entity>& entities);

	size_t GetEntityCount() const noexcept;

	size_t GetRenderableCount() const noexcept;

	size_t GetLightCount() const noexcept;

	bool HasEntity(const Entity entity) const noexcept;

	void Serialize(vz::Archive& archive);
}