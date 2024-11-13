#include "Components.h"
#include "Utils/Backlog.h"
#include "Utils/ECS.h"
#include "Utils/JobSystem.h"
#include "Utils/Platform.h"
#include "Common/Archive.h"
#include "Libs/Geometrics.h"
#include "Common/Backend/GRendererInterface.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <memory>
#include <unordered_map>


namespace vz
{
	extern GraphicsPackage graphicsPackage;

	struct SceneDetails : Scene
	{
		SceneDetails(const Entity entity, const std::string& name) : Scene(entity, name) {};

		// Here, "stream" refers to the GPU memory upload via a single upload operation
		// the process is 1. gathering, 2. streaming up (sync/async)

		// AABB culling streams:
		std::vector<geometrics::AABB> parallelBounds;
		//std::vector<primitive::AABB> aabbRenderables;
		//std::vector<primitive::AABB> aabbLights;

		inline void RunTransformUpdateSystem(jobsystem::context& ctx);
		inline void RunRenderableUpdateSystem(jobsystem::context& ctx);
		inline void RunLightUpdateSystem(jobsystem::context& ctx);
	};
}

namespace vz
{
	const uint32_t SMALL_SUBTASK_GROUPSIZE = 64u;

	using namespace graphics;
	using namespace geometrics;

	Scene::Scene(const Entity entity, const std::string& name) : entity_(entity), name_(name)
	{
		handlerScene_ = graphicsPackage.pluginNewGScene(this);
		assert(handlerScene_->version == GScene::GScene_INTERFACE_VERSION);
	}

	Scene::~Scene()
	{
		handlerScene_->Destroy();
		delete handlerScene_;
		handlerScene_ = nullptr;
	}

	void SceneDetails::RunTransformUpdateSystem(jobsystem::context& ctx)
	{
		//size_t num_transforms = compfactory::GetTransformComponents(renderables_, transforms);
		//assert(num_transforms == GetRenderableCount());

		jobsystem::Dispatch(ctx, (uint32_t)renderables_.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			//TransformComponent* transform = transforms[args.jobIndex];
			TransformComponent* transform = compfactory::GetTransformComponent(renderables_[args.jobIndex]);
			transform->UpdateMatrix();
			});
	}
	void SceneDetails::RunRenderableUpdateSystem(jobsystem::context& ctx)
	{
		parallelBounds.clear();
		parallelBounds.resize((size_t)jobsystem::DispatchGroupCount((uint32_t)renderables_.size(), SMALL_SUBTASK_GROUPSIZE));

		jobsystem::Dispatch(ctx, (uint32_t)renderables_.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			Entity entity = renderables_[args.jobIndex];

			TransformComponent* transform = compfactory::GetTransformComponent(entity);
			if (transform == nullptr)
				return;
			transform->UpdateWorldMatrix();

			RenderableComponent* renderable = compfactory::GetRenderableComponent(entity);
			if (renderable)
			{
				renderable->Update();	// AABB

				if (renderable->IsMeshRenderable() || renderable->IsVolumeRenderable())
				{
					AABB aabb = renderable->GetAABB();

					AABB* shared_bounds = (AABB*)args.sharedmemory;
					if (args.isFirstJobInGroup)
					{
						*shared_bounds = aabb;
					}
					else
					{
						*shared_bounds = AABB::Merge(*shared_bounds, aabb);
					}
					if (args.isLastJobInGroup)
					{
						parallelBounds[args.groupID] = *shared_bounds;
					}
				}
			}
		}, sizeof(geometrics::AABB));
	}

	void SceneDetails::RunLightUpdateSystem(jobsystem::context& ctx)
	{
		jobsystem::Dispatch(ctx, (uint32_t)lights_.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			Entity entity = lights_[args.jobIndex];

			TransformComponent* transform = compfactory::GetTransformComponent(entity);
			if (transform == nullptr)
				return;
			transform->UpdateWorldMatrix();

			LightComponent* light = compfactory::GetLightComponent(entity);
			if (light)
			{
				light->Update();	// AABB
			}
			});
	}

	void Scene::Update(const float dt)
	{
		dt_ = dt;

		SceneDetails* scene_details = static_cast<SceneDetails*>(this);

		// 1. fully CPU-based operations
		jobsystem::context ctx;

		// TODO:
		// * need to consider the scene update time (timestamp)
		//		to avoid unnecessary execution of update systems

		scanGeometryEntities();
		scanMaterialEntities();

		{
			// CHECK if skipping is available
			// check all renderables update time (and their components) //
			// check all lights update time 
			// check scene's update time
			//		compared to
			//		recentUpdateTime_
			// if no changes, then skip this update process
		}
		
		// TODO:
		// * if the following ctx per dependency has just one job,
		//		it would be better use a single thread code without jobsystem

		scene_details->RunTransformUpdateSystem(ctx);
		jobsystem::Wait(ctx); // dependencies
		scene_details->RunRenderableUpdateSystem(ctx);
		scene_details->RunLightUpdateSystem(ctx);
		jobsystem::Wait(ctx); // dependencies

		// Merge parallel bounds computation (depends on object update system):
		aabb_ = AABB();
		for (auto& group_bound : scene_details->parallelBounds)
		{
			aabb_ = AABB::Merge(aabb_, group_bound);
		}

		// TODO: animation updates

		// GPU updates
		// note: since tasks in ctx has been completed
		//		there is no need to pass ctx as an argument.
		handlerScene_->Update(dt);

		isDirty_ = false;
		recentUpdateTime_ = TimerNow;
	}

	void Scene::Clear()
	{
		renderables_.clear();
		lights_.clear();
		lookupRenderables_.clear();
		lookupLights_.clear();

		SceneDetails* scene_details = static_cast<SceneDetails*>(this);
		//scene_details->aabbRenderables.clear();
		//scene_details->aabbLights.clear();
		//scene_details->aabbDecals.clear();
		//
		//scene_details->matrixRenderables.clear();
		//scene_details->matrixRenderablesPrev.clear();

		isDirty_ = true;
	}

	void Scene::AddEntity(const Entity entity)
	{
		if (compfactory::ContainRenderableComponent(entity))
		{
			if (lookupRenderables_.count(entity) == 0)
			//if (!lookupRenderables_.contains(entity))
			{
				lookupRenderables_[entity] = renderables_.size();
				renderables_.push_back(entity);
				isDirty_ = true;
				timeStampSetter_ = TimerNow;
			}
		}
		if (compfactory::ContainLightComponent(entity))
		{
			if (lookupLights_.count(entity) == 0)
			//if (!lookupLights_.contains(entity))
			{
				lookupLights_[entity] = renderables_.size();
				lights_.push_back(entity);
				isDirty_ = true;
				timeStampSetter_ = TimerNow;
			}
		}
	}
	
	void Scene::AddEntities(const std::vector<Entity>& entities)
	{
		for (Entity ett : entities)
		{
			AddEntity(ett); // isDirty_ = true;
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
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}

	void Scene::RemoveEntities(const std::vector<Entity>& entities)
	{
		for (Entity ett : entities)
		{
			Remove(ett); // isDirty_ = true;
		}
	}

	size_t Scene::scanGeometryEntities() noexcept
	{
		geometries_.clear();
		for (auto& ett : renderables_)
		{
			RenderableComponent* renderable = compfactory::GetRenderableComponent(ett);
			if (renderable)
			{
				Entity entity = renderable->GetGeometry();
				GeometryComponent* geometry = compfactory::GetGeometryComponent(entity);
				if (geometry)
				{
					geometries_.push_back(entity);
				}
			}
		}
		return geometries_.size();
	}

	size_t Scene::scanMaterialEntities() noexcept
	{
		materials_.clear();
		for (auto& ett : renderables_)
		{
			RenderableComponent* renderable = compfactory::GetRenderableComponent(ett);
			if (renderable)
			{
				std::vector<Entity> renderable_materials = renderable->GetMaterials();
				materials_.insert(materials_.end(), renderable_materials.begin(), renderable_materials.end());
			}
		}
		return materials_.size();
	}

	bool Scene::HasEntity(const Entity entity) const noexcept
	{
		return lookupLights_.count(entity) > 0 || lookupRenderables_.count(entity) > 0;
		//return lookupLights_.contains(entity) || lookupRenderables_.contains(entity);
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

			archive >> ambient_;

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

			archive << ambient_;

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
			if (it.second->GetSceneName() == name) return it.second.get();
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

	bool Scene::DestroyScene(const Entity entity)
	{
		auto it = scenes.find(entity);
		if (it == scenes.end())
		{
			backlog::post("Scene::DestroyScene >> Invalid Entity! " + stringEntity(entity), backlog::LogLevel::Error);
			return false;
		}
		it->second.reset();
		scenes.erase(it);
		return true;
	}

	void Scene::DestroyAll()
	{
		scenes.clear();
	}
}