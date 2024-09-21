#include "Components.h"
#include "Utils/Backlog.h"
#include "Utils/ECS.h"
#include "Utils/JobSystem.h"
#include "Common/Archive.h"
#include "Common/Backend/GBackend.h"
#include "Common/Backend/GBackendDevice.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <memory>
#include <unordered_map>

namespace vz
{
	static std::unordered_map<Entity, std::unique_ptr<Scene>> scenes;

	Scene* Scene::GetScene(const Entity entity) {
		auto it = scenes.find(entity);
		return it != scenes.end() ? it->second.get() : nullptr;
	}
	Scene* Scene::GetFirstSceneByName(const std::string& name) {
		for (auto& it : scenes) {
			if (it.second->GetName() == name) return it.second.get();
		}
		return nullptr;
	}
	Scene* Scene::GetSceneIncludingEntity(const Entity entity)
	{
		for (size_t i = 0, n = scenes.size(); i < n; ++i)
		{
			Scene* scene = scenes[i].get();
			if (scene->HasEntity(entity))
			{
				return scene;
			}
		}
		return nullptr;
	}
	Scene* Scene::CreateScene(const std::string& name, const Entity entity)
	{
		Entity ett = entity;
		if (entity == 0)
		{
			ett = ecs::CreateEntity();
		}

		scenes[ett] = std::make_unique<Scene>(ett, name);
		return scenes[ett].get();
	}
	void Scene::RemoveEntityForScenes(const Entity entity)
	{
		for (size_t i = 0, n = scenes.size(); i < n; ++i)
		{
			scenes[i]->Remove(entity);
		}
	}
}

namespace vz
{
	class GScene
	{
		inline static const std::string GScene_INTERFACE_VERSION = "20240921";
		// this will be a component of vz::Scene
	protected:
		Scene* scene_ = nullptr;
	public:
		GScene(Scene* scene) : scene_(scene) {}
		~GScene() { Destory(); }

		virtual bool Update(const float dt) = 0;
		virtual bool Destory() = 0;
	};

	Scene::Scene(const Entity entity, const std::string& name) : entity_(entity), name_(name)
	{

	}

	void Scene::Update(const float dt)
	{
		dt_ += dt;

		// to do .. if required for Scene-alone process wo/ GPU processing
	}

	void Scene::AddEntity(const Entity entity)
	{
		if (compfactory::ContainRenderableComponent(entity))
		{
			if (!lookupRenderables_.contains(entity))
			{
				lookupRenderables_[entity] = renderables_.size();
				renderables_.push_back(entity);
			}
		}
		if (compfactory::ContainLightComponent(entity))
		{
			if (!lookupLights_.contains(entity))
			{
				lookupLights_[entity] = renderables_.size();
				lights_.push_back(entity);
			}
		}
	}
	
	void Scene::AddEntities(const std::vector<Entity>& entities)
	{
		for (Entity ett : entities)
		{
			AddEntity(ett);
		}
	}

	void Scene::Remove(const Entity entity)
	{
		auto remove_entity = [](std::unordered_map<Entity, size_t>& lookup, std::vector<Entity>& linearArray, Entity entity)
			{
				auto it = lookup.find(entity);
				if (it != lookup.end())
				{
					size_t index = it->second;
					lookup.erase(it);

					if (index != linearArray.size() - 1)
					{
						linearArray[index] = linearArray.back();
					}
					linearArray.pop_back();
				}
			};

		remove_entity(lookupRenderables_, renderables_, entity);
		remove_entity(lookupLights_, lights_, entity);
	}

	void Scene::RemoveEntities(const std::vector<Entity>& entities)
	{
		for (Entity ett : entities)
		{
			Remove(ett);
		}
	}

	size_t Scene::GetEntityCount() const noexcept
	{
		return renderables_.size() + lights_.size();
	}

	size_t Scene::GetRenderableCount() const noexcept
	{
		return renderables_.size();
	}

	size_t Scene::GetLightCount() const noexcept
	{
		return lights_.size();
	}

	bool Scene::HasEntity(const Entity entity) const noexcept
	{
		return lookupLights_.contains(entity) || lookupRenderables_.contains(entity);
	}

	void Scene::Serialize(vz::Archive& archive)
	{
		// renderables and lights only
		if (archive.IsReadMode())
		{
			std::string seri_name;
			archive >> seri_name;
			assert(seri_name == "Scene");
			archive >> name_;

			size_t num_renderables;
			archive >> num_renderables;
			for (size_t i = 0; i < num_renderables; ++i)
			{
				VUID vuid;
				archive >> vuid;
				Entity entity = compfactory::GetEntityByVUID(vuid);
				assert(compfactory::ContainRenderableComponent(entity));
				AddEntity(entity);
			}

			size_t num_lights;
			archive >> num_lights;
			for (size_t i = 0; i < num_lights; ++i)
			{
				VUID vuid;
				archive >> vuid;
				Entity entity = compfactory::GetEntityByVUID(vuid);
				assert(compfactory::ContainLightComponent(entity));
				AddEntity(entity);
			}
		}
		else
		{
			archive << "Scene";
			archive << name_;

			archive << renderables_.size();
			for (Entity entity : renderables_)
			{
				RenderableComponent* comp = compfactory::GetRenderableComponent(entity);
				assert(comp);
				archive << comp->GetVUID();
			}
			archive << lights_.size();
			for (Entity entity : lights_)
			{
				LightComponent* comp = compfactory::GetLightComponent(entity);
				assert(comp);
				archive << comp->GetVUID();
			}
		}
	}
}