#include "Components.h"
#include "Utils/Backlog.h"
#include "Utils/ECS.h"
#include "Utils/JobSystem.h"
#include "Utils/Platform.h"
#include "Common/Archive.h"
#include "Libs/PrimitiveHelper.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <memory>
#include <unordered_map>

extern GEngineConfig gEngine;

namespace vz
{

	struct SceneDetails : Scene
	{
		SceneDetails(const Entity entity, const std::string& name) : Scene(entity, name) {};

		// Here, "stream" refers to the GPU memory upload via a single upload operation
		// the process is 1. gathering, 2. streaming up (sync/async)

		// AABB culling streams:
		std::vector<geometry::AABB> aabbObjects;
		std::vector<geometry::AABB> aabbLights;

		// Separate stream of world matrices:
		std::vector<XMFLOAT4X4> matrixRenderables;
		std::vector<XMFLOAT4X4> matrixRenderablesPrev;

		inline void RunTransformUpdateSystem(jobsystem::context& ctx);
		inline void RunSceneComponentUpdateSystem(jobsystem::context& ctx);
	};
}

namespace vz
{
	const uint32_t small_subtask_groupsize = 64u;

	namespace graphics
	{
		struct GScene
		{
			inline static const std::string GScene_INTERFACE_VERSION = "GScene::20240921";
			// this will be a component of vz::Scene
		protected:
			Scene* scene_ = nullptr;
		public:
			std::string version = GScene_INTERFACE_VERSION;

			GScene(Scene* scene) : scene_(scene) {}

			virtual bool Update(const float dt) = 0;
			virtual bool Destory() = 0;
		};
	}

	using namespace graphics;
	using namespace geometry;

	typedef GScene* (*PI_NewGScene)(Scene* scene);
	PI_NewGScene graphicsNewGScene = nullptr;

	Scene::Scene(const Entity entity, const std::string& name) : entity_(entity), name_(name)
	{
		if (graphicsNewGScene == nullptr)
		{
			if (gEngine.api == "DX12")
			{
				graphicsNewGScene = platform::LoadModule<PI_NewGScene>("RendererDX12", "NewGScene");
			}
		}
		assert(graphicsNewGScene);
		handlerScene_ = graphicsNewGScene(this);
		assert(handlerScene_->version == GScene::GScene_INTERFACE_VERSION);
	}

	void SceneDetails::RunTransformUpdateSystem(jobsystem::context& ctx)
	{
		std::vector<TransformComponent*> transforms;
		size_t num_transforms = compfactory::GetTransformComponents(renderables_, transforms);
		assert(num_transforms == GetRenderableCount());
		jobsystem::Dispatch(ctx, (uint32_t)num_transforms, small_subtask_groupsize, [&](jobsystem::JobArgs args) {

			TransformComponent* transform = transforms[args.jobIndex];
			transform->UpdateMatrix();
			});
	}
	void SceneDetails::RunSceneComponentUpdateSystem(jobsystem::context& ctx)
	{
		std::vector<HierarchyComponent*> hierarchy;
		size_t num_hierarchies = compfactory::GetHierarchyComponents(renderables_, hierarchy);
		assert(num_hierarchies == GetRenderableCount());
		
		jobsystem::Dispatch(ctx, (uint32_t)num_hierarchies, small_subtask_groupsize, [&](jobsystem::JobArgs args) {

			HierarchyComponent& hier = *hierarchy[args.jobIndex];
			Entity entity = hier.GetEntity();

			TransformComponent* transform_child = compfactory::GetTransformComponent(entity);
			if (transform_child == nullptr)
				return;
			transform_child->UpdateWorldMatrix();

			RenderableComponent* renderable_child = compfactory::GetRenderableComponent(entity);
			if (renderable_child)
			{
				renderable_child->matWorld = transform_child->GetWorldMatrix();
			}

			LightComponent* light_child = compfactory::GetLightComponent(entity);
			if (renderable_child)
			{
				light_child->Update();
			}
		});
	}

	void Scene::Update(const float dt)
	{
		dt_ += dt;

		SceneDetails* scene_details = static_cast<SceneDetails*>(this);

		// 1. fully CPU-based operations
		jobsystem::context ctx;

		// TODO:
		// need to consider the scene update time (timestamp)
		//	to avoid unnecessary execution of update systems

		scene_details->RunTransformUpdateSystem(ctx);
		jobsystem::Wait(ctx); // dependencies
		scene_details->RunSceneComponentUpdateSystem(ctx);
		jobsystem::Wait(ctx); // dependencies

		// TODO: animation updates

		// GPU updates
		// note: since tasks in ctx has been completed
		//		there is no need to pass ctx as an argument.
		handlerScene_->Update(dt);
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

namespace vz
{
	static std::unordered_map<Entity, std::unique_ptr<SceneDetails>> scenes;

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

		scenes[ett] = std::make_unique<SceneDetails>(ett, name);
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