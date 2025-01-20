#include "Components.h"
#include "Utils/Backlog.h"
#include "Utils/ECS.h"
#include "Utils/JobSystem.h"
#include "Utils/Platform.h"
#include "Common/Archive.h"
#include "Libs/Geometrics.h"
#include "Common/Backend/GRendererInterface.h"
#include "Common/ResourceManager.h"

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
		inline void RunGeometryUpdateSystem(jobsystem::context& ctx);
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

		size_t num_renderables = renderables_.size();
		parallelBounds.resize((size_t)jobsystem::DispatchGroupCount((uint32_t)num_renderables, SMALL_SUBTASK_GROUPSIZE));

		matrixRenderables_.resize(num_renderables);
		matrixRenderablesPrev_.resize(num_renderables);
		aabbRenderables_.resize(num_renderables);

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

				AABB aabb = renderable->GetAABB();
				aabbRenderables_[args.jobIndex] = aabb;
				matrixRenderablesPrev_[args.jobIndex] = matrixRenderables_[args.jobIndex];
				matrixRenderables_[args.jobIndex] = transform->GetWorldMatrix();

				if (renderable->IsMeshRenderable() || renderable->IsVolumeRenderable())
				{

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

				if (renderable->IsVolumeRenderable())
				{
					MaterialComponent* material = compfactory::GetMaterialComponent(renderable->GetMaterial(0));
					assert(material);

					GVolumeComponent* volume = (GVolumeComponent*)compfactory::GetVolumeComponentByVUID(
						material->GetVolumeTextureVUID(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP));
					assert(volume);
					assert(volume->IsValidVolume());

					TextureComponent* otf = compfactory::GetTextureComponentByVUID(
						material->GetLookupTableVUID(MaterialComponent::LookupTableSlot::LOOKUP_OTF));
					assert(otf);
					Entity entity_otf = otf->GetEntity();

					if (!volume->GetBlockTexture().IsValid())
					{
						volume->UpdateVolumeMinMaxBlocks({ 8, 8, 8 });
					}
					XMFLOAT2 tableValidBeginEndRatioX = otf->GetTableValidBeginEndRatioX();
					if (!volume->GetVisibleBitmaskBuffer(entity_otf).IsValid()
						|| volume->GetTimeStamp() < otf->GetTimeStamp())
					{
						volume->UpdateVolumeVisibleBlocksBuffer(entity_otf);
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

	void SceneDetails::RunGeometryUpdateSystem(jobsystem::context& ctx)
	{
		jobsystem::Dispatch(ctx, (uint32_t)geometries_.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

			Entity entity = geometries_[args.jobIndex];

			GeometryComponent* geometry = compfactory::GetGeometryComponent(entity);
			assert(geometry != nullptr);

			//if (geometry->busyUpdateBVH->load())
			//	return;
			//geometry->busyUpdateBVH->store(true);
			if (!geometry->IsBVHEnabled())
			{
				geometry->UpdateBVH(true);
			}
			//geometry->busyUpdateBVH->store(false);

			});
	}

	void Scene::Update(const float dt)
	{
		dt_ = dt;

		SceneDetails* scene_details = static_cast<SceneDetails*>(this);

		scanGeometryEntities();
		scanMaterialEntities();

		// 1. fully CPU-based operations
		jobsystem::context ctx_bvh;
		scene_details->RunGeometryUpdateSystem(ctx_bvh);
		
		jobsystem::context ctx;

		// TODO:
		// * need to consider the scene update time (timestamp)
		//		to avoid unnecessary execution of update systems

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

	Scene::RayIntersectionResult Scene::Intersects(const Ray& ray, uint32_t filterMask, uint32_t layerMask, uint32_t lod) const
	{
		using Primitive = GeometryComponent::Primitive;
		RayIntersectionResult result;

		const XMVECTOR ray_origin = XMLoadFloat3(&ray.origin);
		const XMVECTOR ray_direction = XMVector3Normalize(XMLoadFloat3(&ray.direction));

		if ((filterMask & SCU32(RenderableFilterFlags::RENDERABLE_COLLIDER)))
		{
			assert(0 && "COLLIDERCOMPONENT!");
			//
		}
		
		if (filterMask & SCU32(RenderableFilterFlags::RENDERABLE_ALL))
		{
			const size_t renderable_count = renderables_.size();
			for (size_t renderable_index = 0; renderable_index < renderable_count; ++renderable_index)
			{
				Entity entity = renderables_[renderable_index];
				RenderableComponent* renderable = compfactory::GetRenderableComponent(entity);
				assert(renderable);
				const AABB& aabb = renderable->GetAABB();
				if ((layerMask & aabb.layerMask) == 0)
					continue;
				if (!ray.intersects(aabb))
					continue;

				if (renderable->IsMeshRenderable())
				{
					GeometryComponent& geometry = *compfactory::GetGeometryComponent(renderable->GetGeometry());
					if (!geometry.IsBVHEnabled())
						continue;

					const XMMATRIX world_mat = XMLoadFloat4x4(&matrixRenderables_[renderable_index]);
					const XMMATRIX world_mat_prev = XMLoadFloat4x4(&matrixRenderablesPrev_[renderable_index]);
					const XMMATRIX world_mat_inv = XMMatrixInverse(nullptr, world_mat);
					const XMVECTOR ray_origin_local = XMVector3Transform(ray_origin, world_mat_inv);
					const XMVECTOR ray_direction_local = XMVector3Normalize(XMVector3TransformNormal(ray_direction, world_mat_inv));

					const std::vector<Primitive>& parts = geometry.GetPrimitives();
					for (size_t part_index = 0, num_parts = parts.size(); part_index < num_parts; ++part_index)
					{
						const Primitive& part = parts[part_index];
						if (part.GetPrimitiveType() != GeometryComponent::PrimitiveType::TRIANGLES)
							continue;
						assert(part.IsValidBVH());	// this is supposed to be TRUE because geometry.IsAutoUpdateBVH() is TRUE

						const std::vector<XMFLOAT3>& positions = part.GetVtxPositions();
						const std::vector<XMFLOAT3>& normals = part.GetVtxNormals();
						const std::vector<uint32_t>& indices = part.GetIdxPrimives();
						const std::vector<XMFLOAT2>& uvset_0 = part.GetVtxUVSet0();
						const std::vector<XMFLOAT2>& uvset_1 = part.GetVtxUVSet1();
						auto intersectTriangle = [&](const uint32_t subsetIndex, const uint32_t indexOffset, const uint32_t triangleIndex)
							{
								const uint32_t i0 = indices[indexOffset + triangleIndex * 3 + 0];
								const uint32_t i1 = indices[indexOffset + triangleIndex * 3 + 1];
								const uint32_t i2 = indices[indexOffset + triangleIndex * 3 + 2];

								XMVECTOR p0;
								XMVECTOR p1;
								XMVECTOR p2;

								p0 = XMLoadFloat3(&positions[i0]);
								p1 = XMLoadFloat3(&positions[i1]);
								p2 = XMLoadFloat3(&positions[i2]);

								float distance = FLT_MAX;
								XMFLOAT2 bary;
								if (math::RayTriangleIntersects(ray_origin_local, ray_direction_local, p0, p1, p2, distance, bary))
								{
									const XMVECTOR pos_local = XMVectorAdd(ray_origin_local, ray_direction_local * distance);
									const XMVECTOR pos = XMVector3Transform(pos_local, world_mat);
									distance = math::Distance(pos, ray_origin);

									// Note: we do the TMin, Tmax check here, in world space! We use the RayTriangleIntersects in local space, so we don't use those in there
									if (distance < result.distance && distance >= ray.TMin && distance <= ray.TMax)
									{
										XMVECTOR nor;
										if (normals.empty()) // Note: for soft body we compute it instead of loading the simulated normals
										{
											nor = XMVector3Cross(p2 - p1, p1 - p0);
										}
										else
										{
											nor = XMVectorBaryCentric(
												XMLoadFloat3(&normals[i0]),
												XMLoadFloat3(&normals[i1]),
												XMLoadFloat3(&normals[i2]),
												bary.x,
												bary.y
											);
										}
										nor = XMVector3Normalize(XMVector3TransformNormal(nor, world_mat));
										const XMVECTOR vel = pos - XMVector3Transform(pos_local, world_mat_prev);

										result.uv = {};
										if (!uvset_0.empty())
										{
											XMVECTOR uv = XMVectorBaryCentric(
												XMLoadFloat2(&uvset_0[i0]),
												XMLoadFloat2(&uvset_0[i1]),
												XMLoadFloat2(&uvset_0[i2]),
												bary.x,
												bary.y
											);
											result.uv.x = XMVectorGetX(uv);
											result.uv.y = XMVectorGetY(uv);
										}
										if (!uvset_1.empty())
										{
											XMVECTOR uv = XMVectorBaryCentric(
												XMLoadFloat2(&uvset_1[i0]),
												XMLoadFloat2(&uvset_1[i1]),
												XMLoadFloat2(&uvset_1[i2]),
												bary.x,
												bary.y
											);
											result.uv.z = XMVectorGetX(uv);
											result.uv.w = XMVectorGetY(uv);
										}

										result.entity = entity;
										XMStoreFloat3(&result.position, pos);
										XMStoreFloat3(&result.normal, nor);
										XMStoreFloat3(&result.velocity, vel);
										result.distance = distance;
										result.subsetIndex = (int)subsetIndex;
										result.vertexID0 = (int)i0;
										result.vertexID1 = (int)i1;
										result.vertexID2 = (int)i2;
										result.bary = bary;
									}
								}
							};

						Ray ray_local = Ray(ray_origin_local, ray_direction_local);

						const BVH& bvh = part.GetBVH();
						const std::vector<geometrics::AABB>& bvh_leaf_aabbs = part.GetBVHLeafAABBs();
						bvh.Intersects(ray_local, 0, [&](uint32_t index) {
							const AABB& leaf = bvh_leaf_aabbs[index];
							const uint32_t triangleIndex = leaf.layerMask;
							const uint32_t subsetIndex = leaf.userdata;

							intersectTriangle(subsetIndex, 0, triangleIndex);
							});
					}
				}
				else if (renderable->IsVolumeRenderable() 
					&& (filterMask & SCU32(RenderableFilterFlags::RENDERABLE_VOLUME_DVR)))
				{

				}

			}
		}
		result.orientation = ray.GetPlacementOrientation(result.position, result.normal);
		return result;
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
			archive >> skyMapName_;
			archive >> colorGradingMapName_;

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
			archive << skyMapName_;
			archive << colorGradingMapName_;

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
	bool Scene::LoadIBL(const std::string& filename)
	{
		skyMap_ = std::make_shared<Resource>(
			resourcemanager::Load(filename, resourcemanager::Flags::IMPORT_RETAIN_FILEDATA | resourcemanager::Flags::STREAMING)
		);

		skyMapName_ = filename;
		Resource& resource = *skyMap_.get();
		return resource.IsValid();
	}
}

namespace vz
{
	const void* Scene::GetTextureSkyMap() const
	{
		Resource& resource = *skyMap_.get();
		return &resource.GetTexture();
	}
	const void* Scene::GetTextureGradientMap() const
	{
		Resource& resource = *colorGradingMap_.get();
		return &resource.GetTexture();
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
		for (auto& it : scenes)
		{
			it.second->Remove(entity);
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