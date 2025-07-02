#include "GComponents.h"
#include "Utils/Backlog.h"
#include "Utils/ECS.h"
#include "Utils/JobSystem.h"
#include "Utils/Platform.h"
#include "Common/Archive.h"
#include "Utils/Geometrics.h"
#include "GBackend/GModuleLoader.h"
#include "Common/ResourceManager.h"
#include "Common/Engine_Internal.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <memory>
#include <unordered_map>


namespace vz
{
	const uint32_t SMALL_SUBTASK_GROUPSIZE = 64u;
	using namespace graphics;
	using namespace geometrics;
	using RenderableType = RenderableComponent::RenderableType;

	extern GShaderEngineLoader shaderEngine;

#define DOWNCAST ((SceneDetails*)this)

	struct SceneDetails : Scene
	{
		SceneDetails(const Entity entity, const std::string& name) : Scene(entity, name) 
		{
			handlerScene = shaderEngine.pluginNewGScene(this);
			assert(handlerScene->version == GScene::GScene_INTERFACE_VERSION);
		};
		virtual ~SceneDetails()
		{
			handlerScene->Destroy();
			delete handlerScene;
			handlerScene = nullptr;
		}

		//	Note: 
		//		* transform states are based on those streams
		//		* each entity has also TransformComponent and HierarchyComponent
		//	index-map
		std::unordered_map<Entity, uint32_t> lookupRenderables; // All types of renderables
		std::unordered_map<Entity, uint32_t> lookupMeshRenderables;
		std::unordered_map<Entity, uint32_t> lookupVolumeRenderables;
		std::unordered_map<Entity, uint32_t> lookupGSplatRenderables;
		std::unordered_map<Entity, uint32_t> lookupSpriteRenderables;
		std::unordered_map<Entity, uint32_t> lookupSpritefontRenderables;
		std::unordered_map<Entity, uint32_t> lookupLights;
		std::unordered_map<Entity, uint32_t> lookupCameras;
		std::unordered_map<Entity, uint32_t> lookupChildren;

		// AABB culling streams:
		std::vector<geometrics::AABB> aabbRenderables;
		std::vector<geometrics::AABB> aabbLights;
		//std::vector<geometrics::AABB> aabbProbes_;
		//std::vector<geometrics::AABB> aabbDecals_;
		std::vector<geometrics::AABB> parallelBounds;

		std::atomic<uint32_t> geometryAllocator{ 0 }; // for Geometry::Primitive
		std::atomic<uint32_t> instanceResLookupAllocator{ 0 };

		// Separate stream of world matrices:
		std::vector<XMFLOAT4X4> matrixRenderables;
		std::vector<XMFLOAT4X4> matrixRenderablesPrev;
		std::shared_ptr<Resource> skyMap;
		std::shared_ptr<Resource> colorGradingMap;
		GScene* handlerScene = nullptr;

		// Here, "stream" refers to the GPU memory upload via a single upload operation
		// the process is 1. gathering, 2. streaming up (sync/async)

		std::atomic<uint32_t> counterRenderable_Mesh;
		std::atomic<uint32_t> counterRenderable_Volume;
		std::atomic<uint32_t> counterRenderable_GSplat;
		std::atomic<uint32_t> counterRenderable_Sprite;
		std::atomic<uint32_t> counterRenderable_Spritefont;

		// cache for linearized components arrary to avoid compfactory::Get...Component
		std::vector<GRenderableComponent*> renderableComponents;
		std::vector<GRenderableComponent*> renderableMeshComponents;
		std::vector<GRenderableComponent*> renderableVolumeComponents;
		std::vector<GRenderableComponent*> renderableGSplatComponents;
		std::vector<GRenderableComponent*> renderableSpriteComponents;
		std::vector<GRenderableComponent*> renderableSpritefontComponents;
		std::vector<GGeometryComponent*> geometryComponents;
		std::vector<GMaterialComponent*> materialComponents;
		std::vector<GLightComponent*> lightComponents;
		std::vector<CameraComponent*> cameraComponents;

		const std::vector<GRenderableComponent*>& GetRenderableComponents() const override { return renderableComponents; }
		const std::vector<GRenderableComponent*>& GetRenderableMeshComponents() const override { return renderableMeshComponents; }
		const std::vector<GRenderableComponent*>& GetRenderableVolumeComponents() const override { return renderableVolumeComponents; }
		const std::vector<GRenderableComponent*>& GetRenderableGSplatComponents() const override { return renderableGSplatComponents; }
		const std::vector<GRenderableComponent*>& GetRenderableSpriteComponents() const override { return renderableSpriteComponents; }
		const std::vector<GRenderableComponent*>& GetRenderableSpritefontComponents() const override { return renderableSpritefontComponents; }
		const std::vector<GGeometryComponent*>& GetGeometryComponents() const override { return geometryComponents; }
		const std::vector<GMaterialComponent*>& GetMaterialComponents() const override { return materialComponents; }
		const std::vector<GLightComponent*>& GetLightComponents() const override { return lightComponents; }
		const std::vector<CameraComponent*>& GetCameraComponents() const override { return cameraComponents; }

		const uint32_t GetGeometryPrimitivesAllocatorSize() const override { return geometryAllocator.load(); }
		const uint32_t GetRenderableResLookupAllocatorSize() const override { return instanceResLookupAllocator.load(); }

		void RunTransformUpdateSystem(jobsystem::context& ctx)
		{
			jobsystem::Dispatch(ctx, (uint32_t)transforms_.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

				Entity entity = transforms_[args.jobIndex];
				TransformComponent* transform = compfactory::GetTransformComponent(entity);
				transform->UpdateMatrix();

				if (TimeDurationCount(transform->GetTimeStamp(), recentUpdateTime_) > 0)
				{
					isContentChanged_ = true;
				}

				LayeredMaskComponent* layeredmask = compfactory::GetLayeredMaskComponent(entity);
				if (layeredmask)
				{
					if (TimeDurationCount(layeredmask->GetTimeStamp(), recentUpdateTime_) > 0)
					{
						isContentChanged_ = true;
					}
				}

				});
		}
		void RunRenderableUpdateSystem(jobsystem::context& ctx)
		{
			parallelBounds.clear();

			size_t num_renderables = renderables_.size();
			parallelBounds.resize((size_t)jobsystem::DispatchGroupCount((uint32_t)num_renderables, SMALL_SUBTASK_GROUPSIZE));

			matrixRenderables.resize(num_renderables);
			matrixRenderablesPrev.resize(num_renderables);
			aabbRenderables.resize(num_renderables);

			renderableComponents.resize(num_renderables);
			renderableMeshComponents.resize(num_renderables);
			renderableVolumeComponents.resize(num_renderables);
			renderableGSplatComponents.resize(num_renderables);
			renderableSpriteComponents.resize(num_renderables);
			renderableSpritefontComponents.resize(num_renderables);

			counterRenderable_Mesh.store(0);
			counterRenderable_Volume.store(0);
			counterRenderable_GSplat.store(0);
			counterRenderable_Sprite.store(0);
			counterRenderable_Spritefont.store(0);

			instanceResLookupAllocator.store(0u);

			jobsystem::Dispatch(ctx, (uint32_t)renderables_.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

				auto updateSprite = [&](GRenderableComponent* renderable) {

					GSpriteComponent* sprite = (GSpriteComponent*)compfactory::GetSpriteComponent(renderable->GetEntity());
					renderable->sprite = sprite;

					sprite->W = XMLoadFloat4x4(&renderable->transform->GetWorldMatrix());
					XMVECTOR P = XMLoadFloat3(&sprite->GetPosition());
					P = XMVector3TransformCoord(P, sprite->W);
					XMStoreFloat3(&sprite->posW, P);
					if (sprite->IsCameraFacing())
					{
						XMVECTOR S, R, T;
						XMMatrixDecompose(&S, &R, &T, sprite->W);

						XMStoreFloat3(&sprite->scaleW, S);
						XMStoreFloat3(&sprite->translateW, T);
					}

					if (TimeDurationCount(sprite->GetTimeStamp(), recentUpdateTime_) > 0)
					{
						isContentChanged_ = true;
					}

					sprite->texture = (GTextureComponent*)compfactory::GetTextureComponentByVUID(sprite->GetSpriteTextureVUID());
					if (sprite->texture)
					{
						if (TimeDurationCount(sprite->texture->GetTimeStamp(), recentUpdateTime_) > 0)
						{
							isContentChanged_ = true;
						}
					}
					};

				auto updateSpriteFont = [&](GRenderableComponent* renderable) {

					GSpriteFontComponent* font = (GSpriteFontComponent*)compfactory::GetSpriteFontComponent(renderable->GetEntity());
					renderable->spritefont = font;

					font->W = XMLoadFloat4x4(&renderable->transform->GetWorldMatrix());
					XMVECTOR P = XMLoadFloat3(&font->GetPosition());
					P = XMVector3TransformCoord(P, font->W);
					XMStoreFloat3(&font->posW, P);
					if (font->IsCameraFacing())
					{
						XMVECTOR S, R, T;
						XMMatrixDecompose(&S, &R, &T, font->W);

						XMStoreFloat3(&font->scaleW, S);
						XMStoreFloat3(&font->translateW, T);
					}

					if (TimeDurationCount(font->GetTimeStamp(), recentUpdateTime_) > 0)
					{
						isContentChanged_ = true;
					}
					};

				Entity entity = renderables_[args.jobIndex];

				TransformComponent* transform = compfactory::GetTransformComponent(entity);
				assert(transform);
				transform->UpdateWorldMatrix();

				GRenderableComponent* renderable = (GRenderableComponent*)compfactory::GetRenderableComponent(entity);
				assert(renderable);
				renderableComponents[args.jobIndex] = renderable;
				renderable->renderableIndex = args.jobIndex;
				renderable->transform = transform;
				renderable->layeredmask = compfactory::GetLayeredMaskComponent(entity);
				Entity geometry_entity = renderable->GetGeometry();
				renderable->geometry = geometry_entity == INVALID_ENTITY ? nullptr : (GGeometryComponent*)compfactory::GetGeometryComponent(renderable->GetGeometry());
				std::vector<Entity> material_entities = renderable->GetMaterials();
				size_t num_materials = material_entities.size();
				renderable->materials.resize(num_materials);
				for (size_t i = 0; i < num_materials; ++i)
				{
					renderable->materials[i] = (GMaterialComponent*)compfactory::GetMaterialComponent(material_entities[i]);
				}
				
				if (renderable->IsDirty())
				{
					renderable->Update();	// AABB
				}
				if (TimeDurationCount(renderable->GetTimeStamp(), recentUpdateTime_) > 0)
				{
					isContentChanged_ = true;
				}

				renderable->resLookupIndex = ~0u;
				if (!renderable->IsRenderable())
				{
					return;
				}
				// num_materials refers to the number of shaders
				renderable->resLookupIndex = instanceResLookupAllocator.fetch_add(num_materials); // note Sprite/Font has no material

				AABB aabb = renderable->GetAABB();
				aabbRenderables[args.jobIndex] = aabb;
				matrixRenderablesPrev[args.jobIndex] = matrixRenderables[args.jobIndex];
				matrixRenderables[args.jobIndex] = transform->GetWorldMatrix();

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

				RenderableType renderable_type = renderable->GetRenderableType();
				if (renderable_type == RenderableType::VOLUME_RENDERABLE)
				{
					MaterialComponent* material = compfactory::GetMaterialComponent(renderable->GetMaterial(0));

					GVolumeComponent* volume = (GVolumeComponent*)compfactory::GetVolumeComponentByVUID(
						material->GetVolumeTextureVUID(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP));
					assert(volume);
					assert(volume->IsValidVolume());

					if (!volume->GetBlockTexture().IsValid())
					{
						volume->UpdateVolumeMinMaxBlocks({ 8, 8, 8 });
					}

					TimeStamp volume_timeStamp = volume->GetTimeStamp();
					for (size_t i = 0, n = SCU32(MaterialComponent::LookupTableSlot::LOOKUPTABLE_COUNT); i < n; ++i)
					{
						VUID vuid = material->GetLookupTableVUID(static_cast<MaterialComponent::LookupTableSlot>(i));
						if (vuid == INVALID_VUID)
							continue;
						TextureComponent* otf = compfactory::GetTextureComponentByVUID(vuid);
						if (otf == nullptr)
						{
							continue;
						}
						Entity entity_otf = otf->GetEntity();
						XMFLOAT2 tableValidBeginEndRatioX = otf->GetTableValidBeginEndRatioX();
						if (!volume->GetVisibleBitmaskBuffer(entity_otf).IsValid()
							|| volume_timeStamp < otf->GetTimeStamp())
						{
							volume->UpdateVolumeVisibleBlocksBuffer(entity_otf);
						}
					}
				}

				switch (renderable_type)
				{
				case RenderableType::MESH_RENDERABLE:
					renderableMeshComponents[counterRenderable_Mesh.fetch_add(1, std::memory_order_relaxed)] = renderable; 
					break;
				case RenderableType::VOLUME_RENDERABLE:
					renderableVolumeComponents[counterRenderable_Volume.fetch_add(1, std::memory_order_relaxed)] = renderable; 
					break;
				case RenderableType::GSPLAT_RENDERABLE:
					renderableGSplatComponents[counterRenderable_GSplat.fetch_add(1, std::memory_order_relaxed)] = renderable; 
					break;
				case RenderableType::SPRITE_RENDERABLE:
					renderableSpriteComponents[counterRenderable_Sprite.fetch_add(1, std::memory_order_relaxed)] = renderable; 
					updateSprite(renderable);
					break;
				case RenderableType::SPRITEFONT_RENDERABLE:
					renderableSpritefontComponents[counterRenderable_Spritefont.fetch_add(1, std::memory_order_relaxed)] = renderable;
					updateSpriteFont(renderable);
					break;
				default:
					vzlog_assert(0, "MUST BE RENDERABLE!"); return;
				}


				}, sizeof(geometrics::AABB));
		}
		void RunLightUpdateSystem(jobsystem::context& ctx)
		{
			uint32_t num_lights = (uint32_t)lights_.size();
			aabbLights.resize(num_lights);
			lightComponents.resize(num_lights);
			jobsystem::Dispatch(ctx, num_lights, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

				Entity entity = lights_[args.jobIndex];
				TransformComponent* transform = compfactory::GetTransformComponent(entity);
				assert(transform);
				transform->UpdateWorldMatrix();

				GLightComponent* light = (GLightComponent*)compfactory::GetLightComponent(entity);
				assert(light);
				lightComponents[args.jobIndex] = light;
				light->lightIndex = args.jobIndex;
				light->layeredmask = compfactory::GetLayeredMaskComponent(entity);
				light->Update();	// AABB

				AABB aabb = light->GetAABB();
				aabbLights[args.jobIndex] = aabb;

				if (TimeDurationCount(light->GetTimeStamp(), recentUpdateTime_) > 0)
				{
					isContentChanged_ = true;
				}

				});
		}
		void RunGeometryUpdateSystem(jobsystem::context& ctx)
		{
			uint32_t num_geometries = (uint32_t)geometries_.size();
			geometryComponents.resize(num_geometries);
			geometryAllocator.store(0u);
			jobsystem::Dispatch(ctx, num_geometries, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

				Entity entity = geometries_[args.jobIndex];
				GGeometryComponent* geometry = (GGeometryComponent*)compfactory::GetGeometryComponent(entity);
				assert(geometry);
				geometryComponents[args.jobIndex] = geometry;
				geometry->geometryIndex = args.jobIndex;
				geometry->geometryOffset = geometryAllocator.fetch_add((uint32_t)geometry->GetNumParts());

				if (TimeDurationCount(geometry->GetTimeStamp(), recentUpdateTime_) > 0
					|| TimeDurationCount(geometry->timeStampGPUBVHUpdate, recentUpdateTime_) > 0)
				{
					isContentChanged_ = true;
				}
			});
		}
		void RunMaterialUpdateSystem(jobsystem::context& ctx)
		{
			uint32_t num_materials = (uint32_t)materials_.size();
			materialComponents.resize(num_materials);

			jobsystem::Dispatch(ctx, num_materials, SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

				Entity entity = materials_[args.jobIndex];

				GMaterialComponent* material = (GMaterialComponent*)compfactory::GetMaterialComponent(entity);
				assert(material);
				materialComponents[args.jobIndex] = material;
				material->materialIndex = args.jobIndex;

				if (TimeDurationCount(material->GetTimeStamp(), recentUpdateTime_) > 0)
				{
					isContentChanged_ = true;
				}

				for (uint32_t i = 0, n = SCU32(MaterialComponent::TextureSlot::TEXTURESLOT_COUNT); i < n; ++i)
				{
					GTextureComponent* texture = (GTextureComponent*)compfactory::GetTextureComponentByVUID(
						material->GetTextureVUID(static_cast<MaterialComponent::TextureSlot>(i))
					);
					material->textures[i] = texture;
					if (texture)
					{
						if (TimeDurationCount(texture->GetTimeStamp(), recentUpdateTime_) > 0)
						{
							isContentChanged_ = true;
						}
					}
				}
				for (uint32_t i = 0, n = SCU32(MaterialComponent::VolumeTextureSlot::VOLUME_TEXTURESLOT_COUNT); i < n; ++i)
				{
					GVolumeComponent* volume = (GVolumeComponent*)compfactory::GetVolumeComponentByVUID(
						material->GetVolumeTextureVUID(static_cast<MaterialComponent::VolumeTextureSlot>(i))
					);
					material->volumeTextures[i] = volume;
					if (volume)
					{
						if (TimeDurationCount(volume->GetTimeStamp(), recentUpdateTime_) > 0)
						{
							isContentChanged_ = true;
						}
					}
				}
				for (uint32_t i = 0, n = SCU32(MaterialComponent::LookupTableSlot::LOOKUPTABLE_COUNT); i < n; ++i)
				{
					GTextureComponent* texture = (GTextureComponent*)compfactory::GetTextureComponentByVUID(
						material->GetLookupTableVUID(static_cast<MaterialComponent::LookupTableSlot>(i))
					);
					material->textureLookups[i] = texture;
					if (texture)
					{
						if (TimeDurationCount(texture->GetTimeStamp(), recentUpdateTime_) > 0)
						{
							isContentChanged_ = true;
						}
					}
				}
				material->renderableVolumeMapperRenderable = (GRenderableComponent*)compfactory::GetRenderableComponentByVUID(
					material->GetVolumeMapperTargetRenderableVUID()
				);
				});
		}

		// ----- OVERRIDE -----

		GScene* GetGSceneHandle() const override
		{
			return handlerScene;
		}

		bool LoadIBL(const std::string& filename) override
		{
			skyMap = std::make_shared<Resource>(
				resourcemanager::Load(filename, resourcemanager::Flags::IMPORT_RETAIN_FILEDATA | resourcemanager::Flags::STREAMING)
			);

			skyMapName_ = filename;
			Resource& resource = *skyMap.get();
			return resource.IsValid();
		}
		const void* GetTextureSkyMap() const override
		{
			Resource& resource = *skyMap.get();
			return &resource.GetTexture();
		}
		const void* GetTextureGradientMap() const override
		{
			Resource& resource = *colorGradingMap.get();
			return &resource.GetTexture();
		}

		template<typename T>
		void removeEntityLinearArray(std::unordered_map<Entity, uint32_t>& lookup, std::vector<Entity>& linearArray, std::vector<T*>& linearCompArray, Entity entity)
		{
			auto it = lookup.find(entity);
			if (it != lookup.end())
			{
				size_t index = it->second;
				lookup.erase(it);

				if (index != linearArray.size() - 1 && linearArray.size() > 0)
				{
					Entity last_entity = linearArray.back();
					linearArray[index] = last_entity;
					lookup[last_entity] = index;
				}
				linearArray.pop_back();

				if (index != linearCompArray.size() - 1 && linearCompArray.size() > 0)
				{
					auto comp = linearCompArray.back();
					Entity last_entity = comp->GetEntity();
					linearCompArray[index] = comp;
					lookup[last_entity] = index;
				}
				linearCompArray.pop_back();
			}
		}

		void Remove(const Entity entity) override
		{
			auto remove_entity = [](std::unordered_map<Entity, uint32_t>& lookup, std::vector<Entity>& linearArray, Entity entity)
				{
					auto it = lookup.find(entity);
					if (it != lookup.end())
					{
						size_t index = it->second;
						lookup.erase(it);

						if (index != linearArray.size() - 1)
						{
							Entity last_entity = linearArray.back();
							linearArray[index] = last_entity;
							lookup[last_entity] = index;
						}
						linearArray.pop_back();
					}
				};

			remove_entity(lookupTransforms_, transforms_, entity);
			remove_entity(lookupRenderables, renderables_, entity);
			remove_entity(lookupLights, lights_, entity);
			remove_entity(lookupCameras, cameras_, entity);

			remove_entity(lookupChildren, children_, entity);

			// others will be updated via Update()

			timeStampSetter_ = TimerNow;

			//removeEntityLinearArray(lookupTransforms_, transforms_, std::vector<ComponentBase*>(), entity);
			//removeEntityLinearArray(DOWNCAST->lookupRenderables, renderables_, renderableMeshComponents, entity);
			//removeEntityLinearArray(DOWNCAST->lookupLights, lights_, lightComponents, entity);
			//removeEntityLinearArray(DOWNCAST->lookupCameras, cameras_, cameraComponents, entity);
			//
			//removeEntityLinearArray(DOWNCAST->lookupMeshRenderables, std::vector<Entity>(), renderableMeshComponents, entity);
			//removeEntityLinearArray(DOWNCAST->lookupVolumeRenderables, std::vector<Entity>(), renderableVolumeComponents, entity);
			//removeEntityLinearArray(DOWNCAST->lookupGSplatRenderables, std::vector<Entity>(), renderableGSplatComponents, entity);
			//removeEntityLinearArray(DOWNCAST->lookupSpriteRenderables, std::vector<Entity>(), renderableSpriteComponents, entity);
			//removeEntityLinearArray(DOWNCAST->lookupSpritefontRenderables, std::vector<Entity>(), renderableSpritefontComponents, entity);
			//
			//removeEntityLinearArray(DOWNCAST->lookupChildren, children_, std::vector<ComponentBase*>(), entity);

			timeStampSetter_ = TimerNow;
		}

		void scanResourceEntities(const std::vector<Entity>& renderables, std::vector<Entity>& geometries, std::vector<Entity>& materials)
		{
			geometries.clear();
			materials.clear();

			size_t num_renderables = renderables.size();
			if (num_renderables == 0)
			{
				return;
			}

			std::unordered_set<Entity> geometry_set(num_renderables);
			std::unordered_set<Entity> material_set(num_renderables);
			for (auto& ett : renderables)
			{
				RenderableComponent* renderable = compfactory::GetRenderableComponent(ett);
				if (renderable)
				{
					Entity entity = renderable->GetGeometry();
					GeometryComponent* renderable_geometry = compfactory::GetGeometryComponent(entity);
					if (renderable_geometry)
					{
						geometry_set.insert(entity);
					}

					std::vector<Entity> renderable_materials = renderable->GetMaterials();
					material_set.insert(renderable_materials.begin(), renderable_materials.end());
				}
			}
			geometries.reserve(geometry_set.size());
			geometries.insert(geometries.end(), geometry_set.begin(), geometry_set.end());
			materials.reserve(material_set.size());
			materials.insert(materials.end(), material_set.begin(), material_set.end());
		}

		void Update(const float dt) override
		{
			isContentChanged_ = false;
			dt_ = dt;
			deltaTimeAccumulator_ += dt;

			scanResourceEntities(renderables_, geometries_, materials_);

			static jobsystem::context ctx_geometry_bvh; // Must be declared static to prevent context overflow, which could lead to thread access violations
			// note this update needs to be thread-safe

			if (!jobsystem::IsBusy(ctx_geometry_bvh))
			{
				jobsystem::Dispatch(ctx_geometry_bvh, (uint32_t)geometries_.size(), SMALL_SUBTASK_GROUPSIZE, [&](jobsystem::JobArgs args) {

					Entity entity = geometries_[args.jobIndex];
					GGeometryComponent* geometry = (GGeometryComponent*)compfactory::GetGeometryComponent(entity);
					assert(geometry != nullptr);
					bool is_dirty_bvh = geometry->IsDirtyBVH();
					if ((!geometry->HasBVH() || is_dirty_bvh) && !geometry->IsBusyForBVH())
					{
						geometry->UpdateBVH(true);
						isContentChanged_ = true;
					}
					});
			}

			// 1. fully CPU-based operations

			jobsystem::context ctx;

			RunTransformUpdateSystem(ctx);
			jobsystem::Wait(ctx); // dependencies
			RunRenderableUpdateSystem(ctx);
			RunLightUpdateSystem(ctx);
			RunGeometryUpdateSystem(ctx);
			RunMaterialUpdateSystem(ctx);
			jobsystem::Wait(ctx); // dependencies

			renderableMeshComponents.resize(counterRenderable_Mesh.load());
			renderableVolumeComponents.resize(counterRenderable_Volume.load());
			renderableGSplatComponents.resize(counterRenderable_GSplat.load());
			renderableSpriteComponents.resize(counterRenderable_Sprite.load());
			renderableSpritefontComponents.resize(counterRenderable_Spritefont.load());

			// Merge parallel bounds computation (depends on object update system):
			aabb_ = AABB();
			for (auto& group_bound : parallelBounds)
			{
				aabb_ = AABB::Merge(aabb_, group_bound);
			}
			/**/
			// TODO: animation updates

			// GPU updates
			// note: since tasks in ctx has been completed
			//		there is no need to pass ctx as an argument.


			// ?? only when isContentChanged_?
			handlerScene->Update(dt);

			if (!isContentChanged_)
			{
				stableCount++;
			}
			else
			{
				recentUpdateTime_ = TimerNow;
				stableCount = 0;
			}
		}

		uint32_t GetRenderableMeshCount() const override
		{
			return counterRenderable_Mesh.load();
		}
		uint32_t GetRenderableVolumeCount() const override
		{
			return counterRenderable_Volume.load();
		}
		uint32_t GetRenderableGSplatCount() const override
		{
			return counterRenderable_GSplat.load();
		}

		const std::vector<XMFLOAT4X4>& GetRenderableWorldMatrices() const override { return matrixRenderables; }
		const std::vector<XMFLOAT4X4>& GetRenderableWorldMatricesPrev() const override { return matrixRenderablesPrev; }

		const std::vector<geometrics::AABB>& GetRenderableAABBs() const { return aabbRenderables; }
		const std::vector<geometrics::AABB>& GetLightAABBs() const { return aabbLights; }
	};
}

namespace vz
{
	void Scene::Clear()
	{
		transforms_.clear();
		renderables_.clear();
		lights_.clear();
		cameras_.clear();

		children_.clear();
		materials_.clear();
		geometries_.clear();

		lookupTransforms_.clear();

		DOWNCAST->lookupRenderables.clear();
		DOWNCAST->lookupMeshRenderables.clear();
		DOWNCAST->lookupVolumeRenderables.clear();
		DOWNCAST->lookupGSplatRenderables.clear();
		DOWNCAST->lookupSpriteRenderables.clear();
		DOWNCAST->lookupSpritefontRenderables.clear();
		DOWNCAST->lookupLights.clear();
		DOWNCAST->lookupCameras.clear();
		DOWNCAST->lookupChildren.clear();

		DOWNCAST->aabbRenderables.clear();
		DOWNCAST->aabbLights.clear();
		DOWNCAST->parallelBounds.clear();

		DOWNCAST->geometryAllocator.store(0);
		DOWNCAST->instanceResLookupAllocator.store(0);

		DOWNCAST->matrixRenderables.clear();
		DOWNCAST->matrixRenderablesPrev.clear();
		DOWNCAST->skyMap.reset();
		DOWNCAST->colorGradingMap.reset();

		DOWNCAST->counterRenderable_Mesh.store(0);
		DOWNCAST->counterRenderable_Volume.store(0);
		DOWNCAST->counterRenderable_GSplat.store(0);

		DOWNCAST->renderableMeshComponents.clear();
		DOWNCAST->renderableVolumeComponents.clear();
		DOWNCAST->renderableGSplatComponents.clear();
		DOWNCAST->renderableSpriteComponents.clear();
		DOWNCAST->renderableSpritefontComponents.clear();
		DOWNCAST->geometryComponents.clear();
		DOWNCAST->materialComponents.clear();
		DOWNCAST->cameraComponents.clear();
		DOWNCAST->lightComponents.clear();
	}

	void Scene::AddEntity(const Entity entity)
	{
		TransformComponent* transform = compfactory::GetTransformComponent(entity);
		if (transform == nullptr || lookupTransforms_.find(entity) != lookupTransforms_.end())
		{
			vzlog_error("Scene::Invalid Entity, No Transform Entity (%llu)", entity);
			return;
		}

		bool is_attached = false;
		ComponentBase* comp = nullptr;

		std::unordered_map<Entity, uint32_t>& lookupRenderables = DOWNCAST->lookupRenderables;
		//std::unordered_map<Entity, uint32_t>& lookupMeshRenderables = DOWNCAST->lookupMeshRenderables;
		//std::unordered_map<Entity, uint32_t>& lookupVolumeRenderables = DOWNCAST->lookupVolumeRenderables;
		//std::unordered_map<Entity, uint32_t>& lookupGSplatRenderables = DOWNCAST->lookupGSplatRenderables;
		//std::unordered_map<Entity, uint32_t>& lookupSpriteRenderables = DOWNCAST->lookupSpriteRenderables;
		//std::unordered_map<Entity, uint32_t>& lookupSpritefontRenderables = DOWNCAST->lookupSpritefontRenderables;
		std::unordered_map<Entity, uint32_t>& lookupLights = DOWNCAST->lookupLights;
		std::unordered_map<Entity, uint32_t>& lookupCameras = DOWNCAST->lookupCameras;
		std::unordered_map<Entity, uint32_t>& lookupChildren = DOWNCAST->lookupChildren;

		RenderableComponent* renderable = compfactory::GetRenderableComponent(entity);
		lookupTransforms_[entity] = transforms_.size();
		transforms_.push_back(entity);
		if (renderable)
		{
			assert(lookupRenderables.count(entity) == 0);
			lookupRenderables[entity] = renderables_.size();
			renderables_.push_back(entity);
		}
		else if (compfactory::ContainLightComponent(entity))
		{
			assert(lookupLights.count(entity) == 0);
			lookupLights[entity] = lights_.size();
			lights_.push_back(entity);
		}
		else if (compfactory::ContainCameraComponent(entity))
		{
			assert(lookupCameras.count(entity) == 0);
			lookupCameras[entity] = cameras_.size();
			cameras_.push_back(entity);
		}

		HierarchyComponent* hierarchy = compfactory::GetHierarchyComponent(entity);
		assert(hierarchy);
		if (hierarchy->GetParent() == INVALID_VUID)
		{
			lookupChildren[entity] = children_.size();
			children_.push_back(entity);
		}
		timeStampSetter_ = TimerNow;
	}
	
	void Scene::AddEntities(const std::vector<Entity>& entities)
	{
		for (Entity ett : entities)
		{
			AddEntity(ett); // isDirty_ = true;
		}
	}

	void Scene::RemoveEntities(const std::vector<Entity>& entities)
	{
		for (Entity ett : entities)
		{
			Remove(ett); // isDirty_ = true;
		}
	}

	namespace volumeray
	{
		using float4x4 = XMFLOAT4X4;
		using float2 = XMFLOAT2;
		using float3 = XMFLOAT3;
		using float4 = XMFLOAT4;
		using uint = uint32_t;
		using uint2 = XMUINT2;
		using uint3 = XMUINT3;
		using uint4 = XMUINT4;
		using int2 = XMINT2;
		using int3 = XMINT3;
		using int4 = XMINT4;

		static constexpr uint INST_CLIPBOX = 1 << 9;
		static constexpr uint INST_CLIPPLANE = 1 << 10;
		static constexpr uint INST_JITTERING = 1 << 11;

		inline float3 normalize(float3& v)
		{
			XMVECTOR xv = XMLoadFloat3(&v);
			float3 nv;
			XMStoreFloat3(&nv, XMVector3Normalize(xv));
			return nv;
		}

		inline float3 min(const float3& a, const float3& b)
		{
			return float3(std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z));
		}

		inline float3 max(const float3& a, const float3& b)
		{
			return float3(std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z));
		}

		inline float3 safe_rcp3(const float3& v)
		{
			float3 v_rcp;

			if (v.x * v.x < FLT_EPSILON) v_rcp.x = 1000000.f;
			else v_rcp.x = 1.f / v.x;
			
			if (v.y * v.y < FLT_EPSILON) v_rcp.y = 1000000.f;
			else v_rcp.y = 1.f / v.y;

			if (v.z * v.z < FLT_EPSILON) v_rcp.z = 1000000.f;
			else v_rcp.z = 1.f / v.z;

			return v_rcp;
		}

		inline float3 TransformPoint(const float3& pos_src, const float4x4& matT)
		{
			XMMATRIX M = XMLoadFloat4x4(&matT);
			XMVECTOR P = XMLoadFloat3(&pos_src);
			P = XMVector3TransformCoord(P, M);
			float3 out;
			XMStoreFloat3(&out, P);
			return out;
		}

		inline float3 TransformVector(const float3& vec_src, const float4x4& matT)
		{
			XMMATRIX M = XMLoadFloat4x4(&matT);
			XMVECTOR V = XMLoadFloat3(&vec_src);
			V = XMVector3TransformNormal(V, M);
			float3 out;
			XMStoreFloat3(&out, V);
			return out;
		}

		template <typename T>
		inline float SampleVolume(const uint3& iposSampleVS, const uint2& vol_wwh, const T* volumeData)
		{
			return (float)volumeData[vol_wwh.y * iposSampleVS.z + vol_wwh.x * iposSampleVS.y + iposSampleVS.x];
		};

		inline float TrilinearInterpolation(
			float v0, float v1, float v2, float v3,
			float v4, float v5, float v6, float v7,
			const float3& ratio)
		{
			float v01 = v0 * (1.f - ratio.x) + v1 * ratio.x;
			float v23 = v2 * (1.f - ratio.x) + v3 * ratio.x;
			float v0123 = v01 * (1.f - ratio.y) + v23 * ratio.y;
			float v45 = v4 * (1.f - ratio.x) + v5 * ratio.x;
			float v67 = v6 * (1.f - ratio.x) + v7 * ratio.x;
			float v4567 = v45 * (1.f - ratio.y) + v67 * ratio.y;
			float v = v0123 * (1.f - ratio.z) + v4567 * ratio.z;
			return v;
		}

		template <typename T>
		inline float TrilinearSampleVolume(const float3& posSampleVS, const uint2& vol_wwh, const T* volumeData)
		{
			float3 posSampleVS_tmp = float3(posSampleVS.x + 10.f, posSampleVS.y + 10.f, posSampleVS.z + 10.f); // SAFE //
			uint3 int_posSampleVS_tmp = uint3((uint)posSampleVS_tmp.x, (uint)posSampleVS_tmp.y, (uint)posSampleVS_tmp.z);
			float3 ratio = float3(
				posSampleVS_tmp.x - (float)(int_posSampleVS_tmp.x), 
				posSampleVS_tmp.y - (float)(int_posSampleVS_tmp.y),
				posSampleVS_tmp.z - (float)(int_posSampleVS_tmp.z)
			);
			uint3 iposSampleVS = uint3(int_posSampleVS_tmp.x - 10, int_posSampleVS_tmp.y - 10, int_posSampleVS_tmp.z - 10);

			float v0, v1, v2, v3, v4, v5, v6, v7;
			v0 = SampleVolume({ iposSampleVS.x + 0, iposSampleVS.y + 0, iposSampleVS.z + 0 }, vol_wwh, volumeData);
			v1 = SampleVolume({ iposSampleVS.x + 1, iposSampleVS.y + 0, iposSampleVS.z + 0 }, vol_wwh, volumeData);
			v2 = SampleVolume({ iposSampleVS.x + 0, iposSampleVS.y + 1, iposSampleVS.z + 0 }, vol_wwh, volumeData);
			v3 = SampleVolume({ iposSampleVS.x + 1, iposSampleVS.y + 1, iposSampleVS.z + 0 }, vol_wwh, volumeData);
			v4 = SampleVolume({ iposSampleVS.x + 0, iposSampleVS.y + 0, iposSampleVS.z + 1 }, vol_wwh, volumeData);
			v5 = SampleVolume({ iposSampleVS.x + 1, iposSampleVS.y + 0, iposSampleVS.z + 1 }, vol_wwh, volumeData);
			v6 = SampleVolume({ iposSampleVS.x + 0, iposSampleVS.y + 1, iposSampleVS.z + 1 }, vol_wwh, volumeData);
			v7 = SampleVolume({ iposSampleVS.x + 1, iposSampleVS.y + 1, iposSampleVS.z + 1 }, vol_wwh, volumeData);
			return TrilinearInterpolation(v0, v1, v2, v3, v4, v5, v6, v7, ratio);
		};

		template <typename T>
		inline float TrilinearSampleVolume_Safe(const float3& posSampleVS, const float3& vol_size, const uint2& vol_wwh, const T* volumeData)
		{
			if (
				posSampleVS.x < 0 || posSampleVS.x >= vol_size.x - 1.f ||
				posSampleVS.y < 0 || posSampleVS.y >= vol_size.y - 1.f ||
				posSampleVS.z < 0 || posSampleVS.z >= vol_size.z - 1.f
				)
				return 0.f;
			return TrilinearSampleVolume(posSampleVS, vol_wwh, volumeData);
		}

		struct alignas(16) ShaderClipper
		{
			float4x4 transformClibBox; // WS to Clip Box Space (BS), origin-centered unit cube
			float4 clipPlane;

			void Init()
			{
				transformClibBox = math::IDENTITY_MATRIX;
				clipPlane = float4(0, 0, 0, 1);
			}
			void GetCliPlane(float3& pos, float3& vec) const
			{
				XMVECTOR h = XMVectorSet(clipPlane.x, clipPlane.y, clipPlane.z, 0);
				XMStoreFloat3(&vec, XMVector3Normalize(h));
				XMStoreFloat3(&pos, h * ((-clipPlane.w) / XMVectorGetX(XMVector3Dot(h, h))));
			}
		};

		inline float2 ComputeAaBbHits(const float3& pos_start, const float3& pos_min, const float3& pos_max, const float3& vec_dir_rcp)
		{
			// intersect ray with a box
			XMVECTOR invR = XMLoadFloat3(&vec_dir_rcp);
			XMVECTOR xpos_start = XMLoadFloat3(&pos_start);
			XMVECTOR xpos_min = XMLoadFloat3(&pos_min);
			XMVECTOR xpos_max = XMLoadFloat3(&pos_max);

			float3 tbot, ttop;
			XMStoreFloat3(&tbot, XMVectorMultiply(invR, (xpos_min - xpos_start)));
			XMStoreFloat3(&ttop, XMVectorMultiply(invR, (xpos_max - xpos_start)));

			// re-order intersections to find smallest and largest on each axis
			float3 tmin = min(ttop, tbot);
			float3 tmax = max(ttop, tbot);

			// find the largest tmin and the smallest tmax
			float largest_tmin = std::max(std::max(tmin.x, tmin.y), tmin.z);
			float smallest_tmax = std::min(std::min(tmax.x, tmax.y), tmax.z);

			float tnear = std::max(largest_tmin, 0.f);
			float tfar = smallest_tmax;
			return float2(tnear, tfar);
		}

		inline float2 ComputeClipBoxHits(const float3& pos_start, const float3& vec_dir, const float4x4& mat_vbox_2bs)
		{
			float3 pos_src_bs = TransformPoint(pos_start, mat_vbox_2bs);
			float3 vec_dir_bs = TransformVector(vec_dir, mat_vbox_2bs);
			XMVECTOR xvec_dir_bs = XMLoadFloat3(&vec_dir_bs);
			float3 vec_dir_bs_rcp;
			XMStoreFloat3(&vec_dir_bs_rcp, XMVectorReciprocal(xvec_dir_bs));
			float2 hit_t = ComputeAaBbHits(pos_src_bs, float3(-0.5, -0.5, -0.5), float3(0.5, 0.5, 0.5), vec_dir_bs_rcp);
			return hit_t;
		}

		float2 ComputePlaneHits(const float prev_t, const float next_t, const float3& pos_onplane, const float3& vec_plane, 
			const float3& pos_start, const float3& vec_dir)
		{
			float2 hits_t = float2(prev_t, next_t);

			// H : vec_planeSVS, V : f3VecSampleSVS, A : f3PosIPSampleSVS, Q : f3PosPlaneSVS //
			// 0. Is ray direction parallel with plane's vector?
			XMVECTOR xpos_start = XMLoadFloat3(&pos_start);
			XMVECTOR xpos_onplane = XMLoadFloat3(&pos_onplane);
			XMVECTOR xvec_plane = XMLoadFloat3(&vec_plane);
			XMVECTOR xvec_dir = XMLoadFloat3(&vec_dir);
			float dot_HV = XMVectorGetX(XMVector3Dot(xvec_plane, xvec_dir));
			if (dot_HV != 0)
			{
				// 1. Compute T for Position on Plane
				float fT = XMVectorGetX(XMVector3Dot(xvec_plane, xpos_onplane - xpos_start)) / dot_HV;
				// 2. Check if on Cube
				if (fT > prev_t && fT < next_t)
				{
					// 3. Check if front or next position
					if (dot_HV < 0)
						hits_t.x = fT;
					else
						hits_t.y = fT;
				}
				else if (fT > prev_t && fT > next_t)
				{
					if (dot_HV < 0)
						hits_t.y = -FLT_MAX; // return;
					else
						; // conserve prev_t and next_t
				}
				else if (fT < prev_t && fT < next_t)
				{
					if (dot_HV < 0)
						;
					else
						hits_t.y = -FLT_MAX; // return;
				}
			}
			else
			{
				// Check Upperside of plane
				if (XMVectorGetX(XMVector3Dot(xvec_plane, xpos_onplane - xpos_start)) <= 0)
					hits_t.y = -FLT_MAX; // return;
			}

			return hits_t;
		}

		float2 ComputeVBoxHits(const float3& pos_start, const float3& vec_dir, const uint flags,
			const float4x4& mat_vbox_2bs,
			const ShaderClipper& clipper)
		{
			// Compute VObject Box Enter and Exit //
			float2 hits_t = ComputeClipBoxHits(pos_start, vec_dir, mat_vbox_2bs);

			if (hits_t.y > hits_t.x)
			{
				// Custom Clip Plane //
				if (flags & INST_CLIPPLANE)
				{
					float3 pos_clipplane, vec_clipplane;
					clipper.GetCliPlane(pos_clipplane, vec_clipplane);

					float2 hits_clipplane_t = ComputePlaneHits(hits_t.x, hits_t.y, pos_clipplane, vec_clipplane, pos_start, vec_dir);

					hits_t.x = std::max(hits_t.x, hits_clipplane_t.x);
					hits_t.y = std::min(hits_t.y, hits_clipplane_t.y);
				}

				if (flags & INST_CLIPBOX)
				{
					float2 hits_clipbox_t = ComputeClipBoxHits(pos_start, vec_dir, clipper.transformClibBox);

					hits_t.x = std::max(hits_t.x, hits_clipbox_t.x);
					hits_t.y = std::min(hits_t.y, hits_clipbox_t.y);
				}
			}

			return hits_t;
		}

		struct BlockSkip
		{
			bool visible;
			uint num_skip_steps;
		};
		BlockSkip ComputeBlockSkip(const float3 pos_start_ts, const float3 vec_sample_ts_rcp, 
			const float3 singleblock_size_ts, const uint2 blocks_wwh, const uint* buffer_bitmask)
		{
			BlockSkip blk_v = {};
			float3 fblk_id;
			XMStoreFloat3(&fblk_id, XMLoadFloat3(&pos_start_ts) / XMLoadFloat3(&singleblock_size_ts));

			uint3 blk_id = uint3((uint)fblk_id.x, (uint)fblk_id.y, (uint)fblk_id.z);
			uint bitmask_id = blk_id.x + blk_id.y * blocks_wwh.x + blk_id.z * blocks_wwh.y;
			uint mod = bitmask_id % 32u;
			blk_v.visible = (bool)(buffer_bitmask[bitmask_id / 32] & (0x1u << mod));

			float3 pos_min_ts = float3(blk_id.x * singleblock_size_ts.x, blk_id.y * singleblock_size_ts.y, blk_id.z * singleblock_size_ts.z);
			float3 pos_max_ts = float3(
				pos_min_ts.x + singleblock_size_ts.x,
				pos_min_ts.y + singleblock_size_ts.y,
				pos_min_ts.z + singleblock_size_ts.z
				);
			float2 hits_t = ComputeAaBbHits(pos_start_ts, pos_min_ts, pos_max_ts, vec_sample_ts_rcp);
			float dist_skip_ts = hits_t.y - hits_t.x;
			if (dist_skip_ts < 0)
			{
				blk_v.visible = false;
				blk_v.num_skip_steps = 1000000;
			}
			else
			{
				// here, max is for avoiding machine computation error
				blk_v.num_skip_steps = std::max(uint(dist_skip_ts), 1u);	// sample step
			}

			return blk_v;
		};

		template<typename T>
		void SearchForemostSurface(int& step, const float3& pos_ray_start_ws, const float3& dir_sample_ws, const int num_ray_samples, 
			const float4x4& mat_ws2ts, const float3& singleblock_size_ts, const float3& vol_size, const uint2& blocks_wwh,
			const T* volume_data, const uint* buffer_bitmask, const float visible_min_v)
		{
			step = -1;

			float3 pos_ray_start_ts = TransformPoint(pos_ray_start_ws, mat_ws2ts);
			float3 dir_sample_ts = TransformVector(dir_sample_ws, mat_ws2ts);

			float3 dir_sample_ts_rcp = safe_rcp3(dir_sample_ts);

			float3 pos_sample_vs = float3(
				pos_ray_start_ts.x * (vol_size.x - 1.f),
				pos_ray_start_ts.y * (vol_size.y - 1.f),
				pos_ray_start_ts.z * (vol_size.z - 1.f)
			);
			uint2 vol_wwh = uint2((uint)vol_size.x, (uint)vol_size.x * (uint)vol_size.y);

			float sample_v = TrilinearSampleVolume_Safe<T>(pos_sample_vs, vol_size, vol_wwh, volume_data);
			if (sample_v >= visible_min_v)
			{
				step = 0;
				return;
			}

			XMVECTOR xpos_ray_start_ts = XMLoadFloat3(&pos_ray_start_ts);
			XMVECTOR xdir_sample_ts = XMLoadFloat3(&dir_sample_ts);

			for (uint i = 1; i < num_ray_samples; i++)
			{
				float3 pos_sample_ts;
				XMStoreFloat3(&pos_sample_ts, xpos_ray_start_ts + xdir_sample_ts * (float)i);

				BlockSkip blkSkip = ComputeBlockSkip(pos_sample_ts, dir_sample_ts_rcp, singleblock_size_ts, blocks_wwh, buffer_bitmask);
				blkSkip.num_skip_steps = std::min(blkSkip.num_skip_steps, num_ray_samples - i - 1u);

				if (blkSkip.visible)
				{
					for (int k = 0; k < blkSkip.num_skip_steps; k++)
					{
						float3 pos_sample_blk_ts;
						XMStoreFloat3(&pos_sample_blk_ts, xpos_ray_start_ts + xdir_sample_ts * (float)(i + k));
						float3 pos_sample_blk_vs = float3(
							pos_sample_blk_ts.x * (vol_size.x - 1.f),
							pos_sample_blk_ts.y * (vol_size.y - 1.f),
							pos_sample_blk_ts.z * (vol_size.z - 1.f)
						);

						sample_v = TrilinearSampleVolume_Safe<T>(pos_sample_blk_vs, vol_size, vol_wwh, volume_data);
						if (sample_v >= visible_min_v)
						{
							step = i + k;
							i = num_ray_samples;
							k = num_ray_samples;
							break;
						} // if(sample valid check)
					} // for(int k = 0; k < blkSkip.iNumStepSkip; k++, i++)
				}
				//else
				//{
				//    i += blkSkip.num_skip_steps;
				//}
				i += blkSkip.num_skip_steps;
				// this is for outer loop's i++
				//i -= 1;
			}
		}

		bool VolumeActorPicker(float& distance, XMFLOAT4X4& ws2vs, const Ray& ray, const uint flags,
			const TransformComponent* transform, const VolumeComponent* volume, const MaterialComponent* material, const CameraComponent* camera)
		{
			distance = -1.f;

			const XMFLOAT4X4& mat_os2ws = transform->GetWorldMatrix();
			XMMATRIX xmat_os2ws = XMLoadFloat4x4(&mat_os2ws);
			XMMATRIX xmat_ws2os = XMMatrixInverse(NULL, xmat_os2ws);
			XMMATRIX xmat_os2vs = XMLoadFloat4x4(&volume->GetMatrixOS2VS());
			XMMATRIX xmat_vs2ts = XMLoadFloat4x4(&volume->GetMatrixVS2TS());
			XMMATRIX xmat_ws2vs = xmat_ws2os * xmat_os2vs;
			XMMATRIX xmat_ws2ts = xmat_ws2vs * xmat_vs2ts;
			XMStoreFloat4x4(&ws2vs, xmat_ws2vs);
			float4x4 mat_ws2ts;
			XMStoreFloat4x4(&mat_ws2ts, xmat_ws2ts);

			ShaderClipper clipper;
			clipper.Init();

			const geometrics::AABB& aabb_vol = volume->ComputeAABB();
			XMFLOAT3 aabb_size = aabb_vol.getWidth();
			XMMATRIX xmat_s = XMMatrixScaling(1.f / aabb_size.x, 1.f / aabb_size.y, 1.f / aabb_size.z);
			XMMATRIX xmat_alignedvbox_ws2bs = xmat_ws2os * xmat_s;
			XMFLOAT4X4 mat_alignedvbox_ws2bs;
			XMStoreFloat4x4(&mat_alignedvbox_ws2bs, xmat_alignedvbox_ws2bs);

			// Ray Intersection for Clipping Box //
			float2 hits_t = ComputeVBoxHits(ray.origin, ray.direction, flags, mat_alignedvbox_ws2bs, clipper);
			// 1st Exit in the case that there is no ray-intersecting boundary in the volume box

			float z_far;
			camera->GetNearFar(nullptr, &z_far);
			hits_t.y = std::min(z_far, hits_t.y); // only available in orthogonal view (thickness slicer)

			const XMFLOAT3& vox_size = volume->GetVoxelSize();
			const float sample_dist = std::min(std::min(vox_size.x, vox_size.y), vox_size.z);

			int num_ray_samples = (int)((hits_t.y - hits_t.x) / sample_dist + 0.5f);
			if (num_ray_samples <= 0 || num_ray_samples > 100000)
			{
				// (num_ray_samples > 100000) means that camera or volume actor is placed at wrong location (NAN)
				//	so vol_instance.mat_alignedvbox_ws2bs has invalid values
				return false;
			}

			float3 pos_start_ws;
			XMVECTOR S = XMLoadFloat3(&ray.origin);
			XMVECTOR D = XMLoadFloat3(&ray.direction);
			XMStoreFloat3(&pos_start_ws, S + D * hits_t.x);

			float3 dir_sample_ws;
			XMStoreFloat3(&dir_sample_ws, D * sample_dist);

			const uint8_t* volume_data = volume->GetData().data();
			const uint3 ivol_size = uint3(volume->GetWidth(), volume->GetHeight(), volume->GetDepth());
			const float3 vol_size = float3((float)ivol_size.x, (float)ivol_size.y, (float)ivol_size.z);

			MaterialComponent::LookupTableSlot target_lookup_slot = camera->GetDVRLookupSlot();
			GTextureComponent* otf = (GTextureComponent*)compfactory::GetTextureComponentByVUID(material->GetLookupTableVUID(target_lookup_slot));
			assert(otf);
			Entity entity_otf = otf->GetEntity();
			float visible_min_v_ratio = otf->GetTableValidBeginEndRatioX().x;

			GVolumeComponent* Gvolume = (GVolumeComponent*)volume;
			const uint32_t* buffer_bitmask = Gvolume->GetVisibleBitmaskData(entity_otf);
			if (buffer_bitmask == nullptr)
			{
				return false;
			}

			const XMUINT3& blocks_size = Gvolume->GetBlocksSize();
			const XMUINT3& block_pitches = Gvolume->GetBlockPitch();
			const float3 singleblock_size_ts = float3(
				(float)block_pitches.x / (float)vol_size.x,
				(float)block_pitches.y / (float)vol_size.y,
				(float)block_pitches.z / (float)vol_size.z
			);
			const uint2 blocks_wwh = uint2(blocks_size.x, blocks_size.x * blocks_size.y);

			int hit_step = -1;
			switch (volume->GetVolumeFormat())
			{
			case VolumeComponent::VolumeFormat::UINT8:
				SearchForemostSurface<uint8_t>(hit_step, pos_start_ws, dir_sample_ws, num_ray_samples, mat_ws2ts,
					singleblock_size_ts, vol_size, blocks_wwh, 
					(uint8_t*)volume_data, buffer_bitmask, visible_min_v_ratio * 255.f);
				break;
			case VolumeComponent::VolumeFormat::UINT16:
				SearchForemostSurface<uint16_t>(hit_step, pos_start_ws, dir_sample_ws, num_ray_samples, mat_ws2ts,
					singleblock_size_ts, vol_size, blocks_wwh, 
					(uint16_t*)volume_data, buffer_bitmask, visible_min_v_ratio * 65535.f);
				break;
			case VolumeComponent::VolumeFormat::FLOAT:
				SearchForemostSurface<float>(hit_step, pos_start_ws, dir_sample_ws, num_ray_samples, mat_ws2ts,
					singleblock_size_ts, vol_size, blocks_wwh, 
					(float*)volume_data, buffer_bitmask, visible_min_v_ratio);
				break;
			default:
				vzlog_warning("Unsupported Volume Format!");
				return false;
			}

			
			if (hit_step < 0)
			{
				return false;
			}

			//XMVECTOR P_HIT = S + D * sample_dist * (float)hit_step;
			distance = sample_dist* (float)hit_step;
			
			return true;
		}
	}

	Scene::RayIntersectionResult Scene::Intersects(const Ray& ray, const Entity entityCamera, uint32_t filterMask, uint32_t layerMask, uint32_t lod) const
	{
		using Primitive = GeometryComponent::Primitive;
		RayIntersectionResult result;

		const XMVECTOR ray_origin = XMLoadFloat3(&ray.origin);
		const XMVECTOR ray_direction = XMVector3Normalize(XMLoadFloat3(&ray.direction));

		const std::vector<XMFLOAT4X4>& matrixRenderables = DOWNCAST->matrixRenderables;
		const std::vector<XMFLOAT4X4>& matrixRenderablesPrev = DOWNCAST->matrixRenderablesPrev;

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
				if (!renderable->IsPickable())
					continue;
				const AABB& aabb = renderable->GetAABB();
				if ((layerMask & aabb.layerMask) == 0)
					continue;
				if (!ray.intersects(aabb))
					continue;

				RenderableType renderable_type = renderable->GetRenderableType();
				switch (renderable->GetRenderableType())
				{
				case RenderableType::MESH_RENDERABLE:
				{
					GeometryComponent& geometry = *compfactory::GetGeometryComponent(renderable->GetGeometry());
					if (!geometry.HasBVH())
						continue;

					const XMMATRIX world_mat = XMLoadFloat4x4(&matrixRenderables[renderable_index]);
					const XMMATRIX world_mat_prev = XMLoadFloat4x4(&matrixRenderablesPrev[renderable_index]);
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
										result.triIndex = (int)triangleIndex;
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
				} break;
				case RenderableType::VOLUME_RENDERABLE:
				{
					if (!(filterMask & SCU32(RenderableFilterFlags::RENDERABLE_VOLUME_DVR)))
					{
						break;
					}
					MaterialComponent* material = compfactory::GetMaterialComponent(renderable->GetMaterial(0));
					assert(material);
					VolumeComponent* volume = compfactory::GetVolumeComponentByVUID(
						material->GetVolumeTextureVUID(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP)
					);
					assert(volume);
					TransformComponent* transform = compfactory::GetTransformComponent(entity);
					CameraComponent* camera = compfactory::GetCameraComponent(entityCamera);

					float hit_distance;
					XMFLOAT4X4 ws2vs;
					if (volumeray::VolumeActorPicker(hit_distance, ws2vs, ray, renderable->GetFlags(), transform, volume, material, camera))
					{
						if (hit_distance < result.distance && hit_distance >= ray.TMin && hit_distance <= ray.TMax)
						{
							result = RayIntersectionResult();
							result.entity = entity;
							result.distance = hit_distance;

							// TODO: mask value
						}
					}
				} break;
				case RenderableType::GSPLAT_RENDERABLE:
				case RenderableType::UNDEFINED:
					break;
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

			size_t num_transforms;
			archive >> num_transforms;
			for (size_t i = 0; i < num_transforms; ++i)
			{
				VUID vuid;
				archive >> vuid;
				Entity entity = compfactory::GetEntityByVUID(vuid);
				assert(compfactory::ContainTransformComponent(entity));
				AddEntity(entity);
			}

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

			size_t num_cameras;
			archive >> num_cameras;
			for (size_t i = 0; i < num_cameras; ++i)
			{
				VUID vuid;
				archive >> vuid;
				Entity entity = compfactory::GetEntityByVUID(vuid);
				assert(compfactory::ContainCameraComponent(entity));
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

			archive << transforms_.size();
			for (Entity entity : transforms_)
			{
				TransformComponent* comp = compfactory::GetTransformComponent(entity);
				assert(comp);
				archive << comp->GetVUID();
			}
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
			archive << cameras_.size();
			for (Entity entity : cameras_)
			{
				CameraComponent* comp = compfactory::GetCameraComponent(entity);
				assert(comp);
				archive << comp->GetVUID();
			}
		}
	}
}

namespace vz::scenefactory
{
	static std::unordered_map<Entity, std::unique_ptr<SceneDetails>> scenes;

	Scene* GetScene(const Entity entity) {
		auto it = scenes.find(entity);
		return it != scenes.end() ? it->second.get() : nullptr;
	}
	Scene* GetFirstSceneByName(const std::string& name) {
		for (auto& it : scenes) {
			if (it.second->GetSceneName() == name) return it.second.get();
		}
		return nullptr;
	}
	Scene* GetSceneIncludingEntity(const Entity entity)
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
	Scene* CreateScene(const std::string& name, const Entity entity)
	{
		std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
		Entity ett = entity;
		if (entity == 0)
		{
			ett = ecs::CreateEntity();
		}

		scenes[ett] = std::make_unique<SceneDetails>(ett, name);
		return scenes[ett].get();
	}
	void RemoveEntityForScenes(const Entity entity)
	{
		std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
		for (auto& it : scenes)
		{
			it.second->Remove(entity);
		}
	}

	bool DestroyScene(const Entity entity)
	{
		std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
		auto it = scenes.find(entity);
		if (it == scenes.end())
		{
			vzlog_error("Scene::DestroyScene >> Invalid Entity! (%llu)", entity);
			return false;
		}
		it->second.reset();
		scenes.erase(it);
		return true;
	}

	void DestroyAll()
	{
		scenes.clear();
	}
}