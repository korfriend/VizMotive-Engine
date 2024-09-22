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

		// AABB culling streams:
		std::vector<geometry::AABB> aabbObjects;
		std::vector<geometry::AABB> aabbLights;

		void RunTransformUpdateSystem(jobsystem::context& ctx);
		void RunHierarchyUpdateSystem(jobsystem::context& ctx);
		void RunLightUpdateSystem(jobsystem::context& ctx);
		void RunRenderableUpdateSystem(jobsystem::context& ctx);
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
	void SceneDetails::RunHierarchyUpdateSystem(jobsystem::context& ctx)
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
		});
	}
	void SceneDetails::RunLightUpdateSystem(jobsystem::context& ctx)
	{
		std::vector<LightComponent*> lights;
		size_t num_lights = compfactory::GetLightComponents(lights_, lights);
		assert(num_lights == GetRenderableCount());

		aabbLights.resize(num_lights);

		jobsystem::Dispatch(ctx, (uint32_t)num_lights, small_subtask_groupsize, [&](jobsystem::JobArgs args) {

			LightComponent& light = *lights[args.jobIndex];
			Entity entity = light.GetEntity();

			TransformComponent* transform = compfactory::GetTransformComponent(entity);
			if (transform == nullptr)
				return;

			AABB& aabb = aabbLights[args.jobIndex];

			light.occlusionquery = -1;	// TODO

			XMFLOAT4X4 mat44 = transform->GetWorldMatrix();
			XMMATRIX W = XMLoadFloat4x4(&mat44);
			XMVECTOR S, R, T;
			XMMatrixDecompose(&S, &R, &T, W);

			XMStoreFloat3(&light.position, T);
			XMStoreFloat4(&light.rotation, R);
			XMStoreFloat3(&light.scale, S);
			XMStoreFloat3(&light.direction, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), W)));

			switch (light.GetLightType())
			{
			default:
			case enums::LightType::DIRECTIONAL:
				XMStoreFloat3(&light.direction, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), W)));
				aabb.createFromHalfWidth(XMFLOAT3(0, 0, 0), XMFLOAT3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()));
				break;
			case enums::LightType::SPOT:
				XMStoreFloat3(&light.direction, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), W)));
				aabb.createFromHalfWidth(light.position, XMFLOAT3(light.GetRange(), light.GetRange(), light.GetRange()));
				break;
			case enums::LightType::POINT:
				XMStoreFloat3(&light.direction, XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(1, 0, 0, 0), W)));
				aabb.createFromHalfWidth(light.position, XMFLOAT3(light.GetRange(), light.GetRange(), light.GetRange()));
				break;
			}

			});
	}
	void SceneDetails::RunRenderableUpdateSystem(jobsystem::context& ctx)
	{
		size_t num_renderables = renderables_.size();
		aabbObjects.resize(num_renderables);
		/*
		matrix_objects.resize(objects.GetCount());
		matrix_objects_prev.resize(objects.GetCount());
		occlusion_results_objects.resize(objects.GetCount());

		meshletAllocator.store(0u);

		parallel_bounds.clear();
		parallel_bounds.resize((size_t)jobsystem::DispatchGroupCount((uint32_t)num_renderables, small_subtask_groupsize));

		jobsystem::Dispatch(ctx, (uint32_t)num_renderables, small_subtask_groupsize, [&](jobsystem::JobArgs args) {

			Entity entity = objects.GetEntity(args.jobIndex);
			ObjectComponent& object = objects[args.jobIndex];
			AABB& aabb = aabb_objects[args.jobIndex];
			GraphicsDevice* device = wi::graphics::GetDevice();

			// Update occlusion culling status:
			OcclusionResult& occlusion_result = occlusion_results_objects[args.jobIndex];
			if (!wi::renderer::GetFreezeCullingCameraEnabled())
			{
				occlusion_result.occlusionHistory <<= 1u; // advance history by 1 frame
				int query_id = occlusion_result.occlusionQueries[queryheap_idx];
				if (queryResultBuffer[queryheap_idx].mapped_data != nullptr && query_id >= 0)
				{
					uint64_t visible = ((uint64_t*)queryResultBuffer[queryheap_idx].mapped_data)[query_id];
					if (visible)
					{
						occlusion_result.occlusionHistory |= 1; // visible
					}
				}
				else
				{
					occlusion_result.occlusionHistory |= 1; // visible
				}
			}
			occlusion_result.occlusionQueries[queryheap_idx] = -1; // invalidate query

			const LayerComponent* layer = layers.GetComponent(entity);
			uint32_t layerMask;
			if (layer == nullptr)
			{
				layerMask = ~0;
			}
			else
			{
				layerMask = layer->GetLayerMask();
			}

			aabb = AABB();
			object.filterMaskDynamic = 0;
			object.sort_bits = {};
			object.SetDynamic(false);
			object.SetRequestPlanarReflection(false);
			object.fadeDistance = object.draw_distance;

			if (object.meshID != INVALID_ENTITY && meshes.Contains(object.meshID) && transforms.Contains(entity))
			{
				// These will only be valid for a single frame:
				object.mesh_index = (uint32_t)meshes.GetIndex(object.meshID);
				const MeshComponent& mesh = meshes[object.mesh_index];

				if (object.IsWetmapEnabled() && !object.wetmap.IsValid())
				{
					GPUBufferDesc desc;
					desc.size = mesh.vertex_positions.size() * sizeof(uint16_t);
					desc.format = Format::R16_UNORM;
					desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
					device->CreateBuffer(&desc, nullptr, &object.wetmap);
					device->SetName(&object.wetmap, "wetmap");
					object.wetmap_cleared = false;
				}
				else if (!object.IsWetmapEnabled() && object.wetmap.IsValid())
				{
					object.wetmap = {};
				}

				const TransformComponent& transform = *transforms.GetComponent(entity);

				XMMATRIX W = XMLoadFloat4x4(&transform.world);
				aabb = mesh.aabb.transform(W);

				if (mesh.IsSkinned() || mesh.IsDynamic())
				{
					object.SetDynamic(true);
					const ArmatureComponent* armature = armatures.GetComponent(mesh.armatureID);
					if (armature != nullptr)
					{
						aabb = AABB::Merge(aabb, armature->aabb);
					}
				}

				ImpostorComponent* impostor = impostors.GetComponent(object.meshID);
				if (impostor != nullptr)
				{
					object.fadeDistance = std::min(object.fadeDistance, impostor->swapInDistance);
				}

				SoftBodyPhysicsComponent* softbody = softbodies.GetComponent(object.meshID);
				if (softbody != nullptr)
				{
					if (wi::physics::IsEnabled())
					{
						// this will be registered as soft body in the next physics update
						softbody->_flags |= SoftBodyPhysicsComponent::SAFE_TO_REGISTER;

						// soft body manipulated with the object matrix
						softbody->worldMatrix = transform.world;
					}

					if (softbody->physicsobject != nullptr)
					{
						// simulation aabb will be used for soft bodies
						aabb = softbody->aabb;

						// soft bodies have no transform, their vertices are simulated in world space
						W = XMMatrixIdentity();
					}
				}

				object.center = aabb.getCenter();
				object.radius = aabb.getRadius();

				// LOD select:
				if (mesh.subsets_per_lod > 0)
				{
					const float distsq = wi::math::DistanceSquared(camera.Eye, object.center);
					const float radius = object.radius;
					const float radiussq = radius * radius;
					if (distsq < radiussq)
					{
						object.lod = 0;
					}
					else
					{
						const float dist = std::sqrt(distsq);
						const float dist_to_sphere = dist - radius;
						object.lod = uint32_t(dist_to_sphere * object.lod_distance_multiplier);
						object.lod = std::min(object.lod, mesh.GetLODCount() - 1);
					}
				}

				union SortBits
				{
					struct
					{
						uint32_t shadertype : MaterialComponent::SHADERTYPE_COUNT;
						uint32_t blendmode : wi::enums::BLENDMODE_COUNT;
						uint32_t doublesided : 1;	// bool
						uint32_t tessellation : 1;	// bool
						uint32_t alphatest : 1;		// bool
						uint32_t customshader : 8;
						uint32_t sort_priority : 4;
					} bits;
					uint32_t value;
				} sort_bits;
				static_assert(sizeof(SortBits) == sizeof(uint32_t));

				sort_bits.bits.tessellation = mesh.GetTessellationFactor() > 0;
				sort_bits.bits.doublesided = mesh.IsDoubleSided();
				sort_bits.bits.sort_priority = object.sort_priority;

				uint32_t first_subset = 0;
				uint32_t last_subset = 0;
				mesh.GetLODSubsetRange(object.lod, first_subset, last_subset);
				for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
				{
					const MeshComponent::MeshSubset& subset = mesh.subsets[subsetIndex];
					const MaterialComponent* material = materials.GetComponent(subset.materialID);

					if (material != nullptr)
					{
						object.filterMaskDynamic |= material->GetFilterMask();

						if (material->HasPlanarReflection())
						{
							object.SetRequestPlanarReflection(true);
						}

						sort_bits.bits.shadertype |= 1 << material->shaderType;
						sort_bits.bits.blendmode |= 1 << material->GetBlendMode();
						sort_bits.bits.doublesided |= material->IsDoubleSided();
						sort_bits.bits.alphatest |= material->IsAlphaTestEnabled();

						int customshader = material->GetCustomShaderID();
						if (customshader >= 0)
						{
							sort_bits.bits.customshader |= 1 << customshader;
						}
					}
				}

				object.sort_bits = sort_bits.value;

				//// Correction matrix for mesh normals with non-uniform object scaling:
				//XMMATRIX worldMatrixInverseTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, W));
				//XMFLOAT4X4 transformIT;
				//XMStoreFloat4x4(&transformIT, worldMatrixInverseTranspose);

				// Create GPU instance data:
				ShaderMeshInstance inst;
				inst.init();
				XMFLOAT4X4 worldMatrixPrev = matrix_objects[args.jobIndex];
				matrix_objects_prev[args.jobIndex] = worldMatrixPrev;
				XMStoreFloat4x4(matrix_objects.data() + args.jobIndex, W);
				XMFLOAT4X4 worldMatrix = matrix_objects[args.jobIndex];

				inst.transformRaw.Create(worldMatrix);
				if (IsFormatUnorm(mesh.position_format) && !mesh.so_pos.IsValid())
				{
					// The UNORM correction is only done for the GPU data!
					XMMATRIX R = mesh.aabb.getUnormRemapMatrix();
					XMStoreFloat4x4(&worldMatrix, R * W);
					XMStoreFloat4x4(&worldMatrixPrev, R * XMLoadFloat4x4(&worldMatrixPrev));
				}
				inst.transform.Create(worldMatrix);
				inst.transformPrev.Create(worldMatrixPrev);

				// Get the quaternion from W because that reflects changes by other components (eg. softbody)
				XMVECTOR S, R, T;
				XMMatrixDecompose(&S, &R, &T, W);
				XMStoreFloat4(&inst.quaternion, R);
				float size = std::max(XMVectorGetX(S), std::max(XMVectorGetY(S), XMVectorGetZ(S)));

				if (object.lightmap.IsValid())
				{
					inst.lightmap = device->GetDescriptorIndex(&object.lightmap, SubresourceType::SRV);
				}
				inst.uid = entity;
				inst.layerMask = layerMask;
				inst.color = wi::math::CompressColor(object.color);
				inst.emissive = wi::math::pack_half3(XMFLOAT3(object.emissiveColor.x * object.emissiveColor.w, object.emissiveColor.y * object.emissiveColor.w, object.emissiveColor.z * object.emissiveColor.w));
				inst.baseGeometryOffset = mesh.geometryOffset;
				inst.baseGeometryCount = (uint)mesh.subsets.size();
				inst.geometryOffset = inst.baseGeometryOffset + first_subset;
				inst.geometryCount = last_subset - first_subset;
				inst.meshletOffset = meshletAllocator.fetch_add(mesh.meshletCount);
				inst.fadeDistance = object.fadeDistance;
				inst.center = object.center;
				inst.radius = object.radius;
				inst.vb_ao = object.vb_ao_srv;
				inst.vb_wetmap = device->GetDescriptorIndex(&object.wetmap, SubresourceType::SRV);
				inst.alphaTest_size = wi::math::pack_half2(XMFLOAT2(1 - object.alphaRef, size));
				inst.SetUserStencilRef(object.userStencilRef);

				std::memcpy(instanceArrayMapped + args.jobIndex, &inst, sizeof(inst)); // memcpy whole structure into mapped pointer to avoid read from uncached memory

				if (TLAS_instancesMapped != nullptr)
				{
					// TLAS instance data:
					RaytracingAccelerationStructureDesc::TopLevel::Instance instance;
					for (int i = 0; i < arraysize(instance.transform); ++i)
					{
						for (int j = 0; j < arraysize(instance.transform[i]); ++j)
						{
							instance.transform[i][j] = worldMatrix.m[j][i];
						}
					}
					instance.instance_id = args.jobIndex;
					instance.instance_mask = layerMask == 0 ? 0 : 0xFF;
					if (!object.IsRenderable() || !mesh.IsRenderable())
					{
						instance.instance_mask = 0;
					}
					if (!object.IsCastingShadow())
					{
						instance.instance_mask &= ~wi::renderer::raytracing_inclusion_mask_shadow;
					}
					if (object.IsNotVisibleInReflections())
					{
						instance.instance_mask &= ~wi::renderer::raytracing_inclusion_mask_reflection;
					}
					instance.bottom_level = &mesh.BLASes[object.lod];
					instance.instance_contribution_to_hit_group_index = 0;
					instance.flags = 0;

					if (mesh.IsDoubleSided() || mesh._flags & MeshComponent::TLAS_FORCE_DOUBLE_SIDED)
					{
						instance.flags |= RaytracingAccelerationStructureDesc::TopLevel::Instance::FLAG_TRIANGLE_CULL_DISABLE;
					}

					if (XMVectorGetX(XMMatrixDeterminant(W)) > 0)
					{
						// There is a mismatch between object space winding and BLAS winding:
						//	https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_raytracing_instance_flags
						instance.flags |= RaytracingAccelerationStructureDesc::TopLevel::Instance::FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
					}

					void* dest = (void*)((size_t)TLAS_instancesMapped + (size_t)args.jobIndex * device->GetTopLevelAccelerationStructureInstanceSize());
					device->WriteTopLevelAccelerationStructureInstance(&instance, dest);
				}

				// lightmap things:
				if (object.IsLightmapRenderRequested() && dt > 0)
				{
					if (!object.lightmap.IsValid())
					{
						object.lightmapWidth = wi::math::GetNextPowerOfTwo(object.lightmapWidth + 1) / 2;
						object.lightmapHeight = wi::math::GetNextPowerOfTwo(object.lightmapHeight + 1) / 2;

						TextureDesc desc;
						desc.width = object.lightmapWidth;
						desc.height = object.lightmapHeight;
						desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
						// Note: we need the full precision format to achieve correct accumulative blending! 
						//	But the final lightmap will be compressed into an optimal format when the rendering is finished
						desc.format = Format::R32G32B32A32_FLOAT;

						device->CreateTexture(&desc, nullptr, &object.lightmap);
						device->SetName(&object.lightmap, "lightmap_renderable");

						object.lightmapIterationCount = 0; // reset accumulation
					}
				}

				if (!object.lightmapTextureData.empty() && !object.lightmap.IsValid())
				{
					// Create a GPU-side per object lightmap if there is none yet, but the data exists already:
					const size_t lightmap_size = object.lightmapTextureData.size();
					if (lightmap_size == object.lightmapWidth * object.lightmapHeight * sizeof(XMFLOAT4))
					{
						object.lightmap.desc.format = Format::R32G32B32A32_FLOAT;
					}
					else if (lightmap_size == object.lightmapWidth * object.lightmapHeight * sizeof(PackedVector::XMFLOAT3PK))
					{
						object.lightmap.desc.format = Format::R11G11B10_FLOAT;
					}
					else if (lightmap_size == (object.lightmapWidth / GetFormatBlockSize(Format::BC6H_UF16)) * (object.lightmapHeight / GetFormatBlockSize(Format::BC6H_UF16)) * GetFormatStride(Format::BC6H_UF16))
					{
						object.lightmap.desc.format = Format::BC6H_UF16;
					}
					else
					{
						assert(0); // unknown data format
					}
					wi::texturehelper::CreateTexture(object.lightmap, object.lightmapTextureData.data(), object.lightmapWidth, object.lightmapHeight, object.lightmap.desc.format);
					device->SetName(&object.lightmap, "lightmap");
				}

				aabb.layerMask = layerMask;

				// parallel bounds computation using shared memory:
				AABB* shared_bounds = (AABB*)args.sharedmemory;
				if (args.isFirstJobInGroup)
				{
					*shared_bounds = aabb_objects[args.jobIndex];
				}
				else
				{
					*shared_bounds = AABB::Merge(*shared_bounds, aabb_objects[args.jobIndex]);
				}
				if (args.isLastJobInGroup)
				{
					parallel_bounds[args.groupID] = *shared_bounds;
				}
			}

			}, sizeof(AABB));
			/**/
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
		scene_details->RunHierarchyUpdateSystem(ctx);
		jobsystem::Wait(ctx); // dependencies
		scene_details->RunLightUpdateSystem(ctx);
		jobsystem::Wait(ctx); // dependencies

		// GPU updates
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