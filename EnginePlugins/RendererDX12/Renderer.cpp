#include "PluginInterface.h"
#include "Renderer.h"

#include "Shaders/ShaderInterop.h"
#include "Components/GComponents.h"
#include "Utils/JobSystem.h"
#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"
#include "Utils/Spinlock.h"
#include "Utils/Profiler.h"
#include "Libs/Math.h"
#include "Libs/PrimitiveHelper.h"
#include "ThirdParty/RectPacker.h"

namespace vz::common
{
	InputLayout			inputLayouts[ILTYPE_COUNT];
	RasterizerState		rasterizers[RSTYPE_COUNT];
	DepthStencilState	depthStencils[DSSTYPE_COUNT];
	BlendState			blendStates[BSTYPE_COUNT];
	Shader				shaders[SHADERTYPE_COUNT];
	GPUBuffer			buffers[BUFFERTYPE_COUNT];
	Sampler				samplers[SAMPLER_COUNT];

	PipelineState		PSO_debug[DEBUGRENDERING_COUNT];
	PipelineState		PSO_mesh[RENDERPASS_COUNT];
	PipelineState		PSO_wireframe;
}

namespace vz::renderer
{
	const float renderingSpeed = 1.f;
	const bool isOcclusionCullingEnabled = true;
	const bool isSceneUpdateEnabled = true;
	const bool isTemporalAAEnabled = false;

	using namespace primitive;

	struct View
	{
		// User fills these:
		uint8_t layerMask = ~0;
		const Scene* scene = nullptr;
		const CameraComponent* camera = nullptr;
		enum FLAGS
		{
			EMPTY = 0,
			ALLOW_RENDERABLES = 1 << 0,
			ALLOW_LIGHTS = 1 << 1,
			//ALLOW_DECALS = 1 << 2,
			//ALLOW_ENVPROBES = 1 << 3,
			//ALLOW_EMITTERS = 1 << 4,
			ALLOW_OCCLUSION_CULLING = 1 << 5,
			//ALLOW_SHADOW_ATLAS_PACKING = 1 << 6,

			ALLOW_EVERYTHING = ~0u
		};
		uint32_t flags = EMPTY;

		primitive::Frustum frustum; // camera's frustum or special purposed frustum
		std::vector<uint32_t> visibleRenderables; // index refers to the linear array of Scnee::renderables
		//std::vector<uint32_t> visibleDecals;
		//std::vector<uint32_t> visibleEnvProbes;
		//std::vector<uint32_t> visibleEmitters;
		std::vector<uint32_t> visibleLights; // index refers to the linear array of Scnee::lights

		//rectpacker::State shadowPacker;
		//std::vector<rectpacker::Rect> visibleLightShadowRects;

		std::atomic<uint32_t> renderableCounter;
		std::atomic<uint32_t> lightCounter;

		vz::SpinLock locker;
		bool isPlanarReflectionVisible = false;
		float closestReflectionPlane = std::numeric_limits<float>::max();
		std::atomic_bool volumetricLightRequest{ false };

		void Clear()
		{
			visibleRenderables.clear();
			visibleLights.clear();
			//visibleDecals.clear();
			//visibleEnvProbes.clear();
			//visibleEmitters.clear();

			renderableCounter.store(0);
			lightCounter.store(0);

			closestReflectionPlane = std::numeric_limits<float>::max();
			volumetricLightRequest.store(false);
		}
		bool IsRequestedVolumetricLights() const
		{
			return volumetricLightRequest.load();
		}
	};
	
	struct ViewResources
	{
		XMUINT2 tile_count = {};
		graphics::GPUBuffer bins;
		graphics::GPUBuffer binned_tiles;
		graphics::Texture texture_payload_0;
		graphics::Texture texture_payload_1;
		graphics::Texture texture_normals;
		graphics::Texture texture_roughness;

		// You can request any of these extra outputs to be written by VisibilityResolve:
		const graphics::Texture* depthbuffer = nullptr; // depth buffer that matches with post projection
		const graphics::Texture* lineardepth = nullptr; // depth buffer in linear space in [0,1] range
		const graphics::Texture* primitiveID_resolved = nullptr; // resolved from MSAA texture_visibility input

		inline bool IsValid() const { return bins.IsValid(); }
	};

	// must be called after scene->update()
	void UpdateView(View& vis)
	{
		// Perform parallel frustum culling and obtain closest reflector:
		jobsystem::context ctx;
		auto range = profiler::BeginRangeCPU("Frustum Culling");

		assert(vis.scene != nullptr); // User must provide a scene!
		assert(vis.camera != nullptr); // User must provide a camera!

		// The parallel frustum culling is first performed in shared memory, 
		//	then each group writes out it's local list to global memory
		//	The shared memory approach reduces atomics and helps the list to remain
		//	more coherent (less randomly organized compared to original order)
		static const uint32_t groupSize = 64;
		static const size_t sharedmemory_size = (groupSize + 1) * sizeof(uint32_t); // list + counter per group

		// Initialize visible indices:
		vis.Clear();

		// TODO : add frustum culling processs
		//if (!GetFreezeCullingCameraEnabled()) // just for debug
		{
			vis.frustum = vis.camera->GetFrustum();
		}
		//if (!GetOcclusionCullingEnabled() || GetFreezeCullingCameraEnabled())
		//{
		//	vis.flags &= ~View::ALLOW_OCCLUSION_CULLING;
		//}

		
		if (vis.flags & View::ALLOW_LIGHTS)
		{
			// Cull lights:
			const uint32_t light_loop = (uint32_t)vis.scene->GetLightCount();
			vis.visibleLights.resize(light_loop);
			//vis.visibleLightShadowRects.clear();
			//vis.visibleLightShadowRects.resize(light_loop);
			jobsystem::Dispatch(ctx, light_loop, groupSize, [&](jobsystem::JobArgs args) {

				const std::vector<Entity>& light_entities = vis.scene->GetLightEntities();
				Entity entity = light_entities[args.jobIndex];
				const LightComponent& light = *compfactory::GetLightComponent(entity);
				assert(!light.IsDirty());

				// Setup stream compaction:
				uint32_t& group_count = *(uint32_t*)args.sharedmemory;
				uint32_t* group_list = (uint32_t*)args.sharedmemory + 1;
				if (args.isFirstJobInGroup)
				{
					group_count = 0; // first thread initializes local counter
				}

				const AABB& aabb = light.GetAABB();

				if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
				{
					if (!light.IsInactive())
					{
						// Local stream compaction:
						//	(also compute light distance for shadow priority sorting)
						group_list[group_count] = args.jobIndex;
						group_count++;
						//if (light.IsVolumetricsEnabled())
						//{
						//	vis.volumetricLightRequest.store(true);
						//}

						//if (vis.flags & View::ALLOW_OCCLUSION_CULLING)
						//{
						//	if (!light.IsStatic() && light.GetType() != LightComponent::DIRECTIONAL || light.occlusionquery < 0)
						//	{
						//		if (!aabb.intersects(vis.camera->Eye))
						//		{
						//			light.occlusionquery = vis.scene->queryAllocator.fetch_add(1); // allocate new occlusion query from heap
						//		}
						//	}
						//}
					}
				}

				// Global stream compaction:
				if (args.isLastJobInGroup && group_count > 0)
				{
					uint32_t prev_count = vis.lightCounter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						vis.visibleLights[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		if (vis.flags & View::ALLOW_RENDERABLES)
		{
			// Cull objects:
			const uint32_t renderable_loop = (uint32_t)vis.scene->GetRenderableCount();
			vis.visibleRenderables.resize(renderable_loop);
			jobsystem::Dispatch(ctx, renderable_loop, groupSize, [&](jobsystem::JobArgs args) {

				const std::vector<Entity>& renderable_entities = vis.scene->GetRenderableEntities();
				Entity entity = renderable_entities[args.jobIndex];
				const RenderableComponent& renderable = *compfactory::GetRenderableComponent(entity);
				assert(!renderable.IsDirty());

				// Setup stream compaction:
				uint32_t& group_count = *(uint32_t*)args.sharedmemory;
				uint32_t* group_list = (uint32_t*)args.sharedmemory + 1;
				if (args.isFirstJobInGroup)
				{
					group_count = 0; // first thread initializes local counter
				}

				const AABB& aabb = renderable.GetAABB();

				if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
				{
					// Local stream compaction:
					group_list[group_count++] = args.jobIndex;

					//Scene::OcclusionResult& occlusion_result = vis.scene->occlusion_results_objects[args.jobIndex];
					//bool occluded = false;
					//if (vis.flags & View::ALLOW_OCCLUSION_CULLING)
					//{
					//	occluded = occlusion_result.IsOccluded();
					//}
					//
					//if (vis.flags & View::ALLOW_OCCLUSION_CULLING)
					//{
					//	if (renderable.IsRenderable() && occlusion_result.occlusionQueries[vis.scene->queryheap_idx] < 0)
					//	{
					//		if (aabb.intersects(vis.camera->Eye))
					//		{
					//			// camera is inside the instance, mark it as visible in this frame:
					//			occlusion_result.occlusionHistory |= 1;
					//		}
					//		else
					//		{
					//			occlusion_result.occlusionQueries[vis.scene->queryheap_idx] = vis.scene->queryAllocator.fetch_add(1); // allocate new occlusion query from heap
					//		}
					//	}
					//}
				}

				// Global stream compaction:
				if (args.isLastJobInGroup && group_count > 0)
				{
					uint32_t prev_count = vis.renderableCounter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						vis.visibleRenderables[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		jobsystem::Wait(ctx);

		// finalize stream compaction: (memory safe)
		vis.visibleRenderables.resize((size_t)vis.renderableCounter.load());
		vis.visibleLights.resize((size_t)vis.lightCounter.load());
		
		profiler::EndRange(range); // Frustum Culling
	}

	void UpdatePerFrameData(
		Scene& scene,
		const View& vis,
		FrameCB& frameCB,
		float dt
	)
	{
		GraphicsDevice* device = graphics::GetDevice();

		// Update CPU-side frame constant buffer:
		frameCB.delta_time = dt * renderingSpeed;
		frameCB.time_previous = frameCB.time;
		frameCB.time += frameCB.delta_time;
		frameCB.frame_count = (uint)device->GetFrameCount();
		frameCB.blue_noise_phase = (frameCB.frame_count & 0xFF) * 1.6180339887f;

		frameCB.temporalaa_samplerotation = 0;
		if (isTemporalAAEnabled)
		{
			uint x = frameCB.frame_count % 4;
			uint y = frameCB.frame_count / 4;
			frameCB.temporalaa_samplerotation = (x & 0x000000FF) | ((y & 0x000000FF) << 8);
		}

		frameCB.options = 0;
		if (isTemporalAAEnabled)
		{
			frameCB.options |= OPTION_BIT_TEMPORALAA_ENABLED;
		}

		/*
		frameCB.scene = vis.scene->shaderscene;

		frameCB.texture_random64x64_index = device->GetDescriptorIndex(texturehelper::getRandom64x64(), SubresourceType::SRV);
		frameCB.texture_bluenoise_index = device->GetDescriptorIndex(texturehelper::getBlueNoise(), SubresourceType::SRV);
		frameCB.texture_sheenlut_index = device->GetDescriptorIndex(&textures[TEXTYPE_2D_SHEENLUT], SubresourceType::SRV);

		// Fill Entity Array with decals + envprobes + lights in the frustum:
		uint envprobearray_offset = 0;
		uint envprobearray_count = 0;
		uint lightarray_offset_directional = 0;
		uint lightarray_count_directional = 0;
		uint lightarray_offset_spot = 0;
		uint lightarray_count_spot = 0;
		uint lightarray_offset_point = 0;
		uint lightarray_count_point = 0;
		uint lightarray_offset = 0;
		uint lightarray_count = 0;
		uint decalarray_offset = 0;
		uint decalarray_count = 0;
		uint forcefieldarray_offset = 0;
		uint forcefieldarray_count = 0;
		frameCB.entity_culling_count = 0;
		{
			ShaderEntity* entityArray = frameCB.entityArray;
			float4x4* matrixArray = frameCB.matrixArray;

			uint32_t entityCounter = 0;
			uint32_t matrixCounter = 0;

			// Write decals into entity array:
			decalarray_offset = entityCounter;
			const size_t decal_iterations = std::min((size_t)MAX_SHADER_DECAL_COUNT, vis.visibleDecals.size());
			for (size_t i = 0; i < decal_iterations; ++i)
			{
				if (entityCounter == SHADER_ENTITY_COUNT)
				{
					entityCounter--;
					break;
				}
				if (matrixCounter >= MATRIXARRAY_COUNT)
				{
					matrixCounter--;
					break;
				}
				ShaderEntity shaderentity = {};
				XMMATRIX shadermatrix;

				const uint32_t decalIndex = vis.visibleDecals[vis.visibleDecals.size() - 1 - i]; // note: reverse order, for correct blending!
				const DecalComponent& decal = vis.scene->decals[decalIndex];

				shaderentity.layerMask = ~0u;

				Entity entity = vis.scene->decals.GetEntity(decalIndex);
				const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->layerMask;
				}

				shaderentity.SetType(ENTITY_TYPE_DECAL);
				if (decal.IsBaseColorOnlyAlpha())
				{
					shaderentity.SetFlags(ENTITY_FLAG_DECAL_BASECOLOR_ONLY_ALPHA);
				}
				shaderentity.position = decal.position;
				shaderentity.SetRange(decal.range);
				float emissive_mul = 1 + decal.emissive;
				shaderentity.SetColor(float4(decal.color.x * emissive_mul, decal.color.y * emissive_mul, decal.color.z * emissive_mul, decal.color.w));
				shaderentity.shadowAtlasMulAdd = decal.texMulAdd;
				shaderentity.SetConeAngleCos(decal.slopeBlendPower);
				shaderentity.SetDirection(decal.front);
				shaderentity.SetAngleScale(decal.normal_strength);
				shaderentity.SetLength(decal.displacement_strength);

				shaderentity.SetIndices(matrixCounter, 0);
				shadermatrix = XMMatrixInverse(nullptr, XMLoadFloat4x4(&decal.world));

				int texture = -1;
				if (decal.texture.IsValid())
				{
					texture = device->GetDescriptorIndex(&decal.texture.GetTexture(), SubresourceType::SRV, decal.texture.GetTextureSRGBSubresource());
				}
				int normal = -1;
				if (decal.normal.IsValid())
				{
					normal = device->GetDescriptorIndex(&decal.normal.GetTexture(), SubresourceType::SRV);
				}
				int surfacemap = -1;
				if (decal.surfacemap.IsValid())
				{
					surfacemap = device->GetDescriptorIndex(&decal.surfacemap.GetTexture(), SubresourceType::SRV);
				}
				int displacementmap = -1;
				if (decal.displacementmap.IsValid())
				{
					displacementmap = device->GetDescriptorIndex(&decal.displacementmap.GetTexture(), SubresourceType::SRV);
				}

				shadermatrix.r[0] = XMVectorSetW(shadermatrix.r[0], *(float*)&texture);
				shadermatrix.r[1] = XMVectorSetW(shadermatrix.r[1], *(float*)&normal);
				shadermatrix.r[2] = XMVectorSetW(shadermatrix.r[2], *(float*)&surfacemap);
				shadermatrix.r[3] = XMVectorSetW(shadermatrix.r[3], *(float*)&displacementmap);

				XMStoreFloat4x4(matrixArray + matrixCounter, shadermatrix);
				matrixCounter++;

				std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
				entityCounter++;
				decalarray_count++;
			}


			// Write directional lights into entity array:
			lightarray_offset = entityCounter;
			lightarray_offset_directional = entityCounter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entityCounter == SHADER_ENTITY_COUNT)
				{
					entityCounter--;
					break;
				}

				const LightComponent& light = vis.scene->lights[lightIndex];
				if (light.GetType() != LightComponent::DIRECTIONAL || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;

				Entity entity = vis.scene->lights.GetEntity(lightIndex);
				const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->layerMask;
				}

				shaderentity.SetType(light.GetType());
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.radius);
				shaderentity.SetLength(light.length);
				shaderentity.SetDirection(light.direction);
				shaderentity.SetColor(float4(light.color.x * light.intensity, light.color.y * light.intensity, light.color.z * light.intensity, 1));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				bool shadow = IsShadowsEnabled() && light.IsCastingShadow() && !light.IsStatic();
				const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];

				if (shadow)
				{
					shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
					shaderentity.SetIndices(matrixCounter, 0);
				}

				const uint cascade_count = std::min((uint)light.cascade_distances.size(), MATRIXARRAY_COUNT - matrixCounter);
				shaderentity.SetShadowCascadeCount(cascade_count);

				if (shadow && !light.cascade_distances.empty())
				{
					SHCAM* shcams = (SHCAM*)alloca(sizeof(SHCAM) * cascade_count);
					CreateDirLightShadowCams(light, *vis.camera, shcams, cascade_count, shadow_rect);
					for (size_t cascade = 0; cascade < cascade_count; ++cascade)
					{
						XMStoreFloat4x4(&matrixArray[matrixCounter++], shcams[cascade].view_projection);
					}
				}

				if (light.IsStatic())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				}

				if (light.IsVolumetricCloudsEnabled())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_VOLUMETRICCLOUDS);
				}

				std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
				entityCounter++;
				lightarray_count_directional++;
			}

			// Write spot lights into entity array:
			lightarray_offset_spot = entityCounter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entityCounter == SHADER_ENTITY_COUNT)
				{
					entityCounter--;
					break;
				}

				const LightComponent& light = vis.scene->lights[lightIndex];
				if (light.GetType() != LightComponent::SPOT || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;

				Entity entity = vis.scene->lights.GetEntity(lightIndex);
				const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->layerMask;
				}

				shaderentity.SetType(light.GetType());
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.radius);
				shaderentity.SetLength(light.length);
				shaderentity.SetDirection(light.direction);
				shaderentity.SetColor(float4(light.color.x * light.intensity, light.color.y * light.intensity, light.color.z * light.intensity, 1));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				bool shadow = IsShadowsEnabled() && light.IsCastingShadow() && !light.IsStatic();
				const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];

				if (shadow)
				{
					shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
					shaderentity.SetIndices(matrixCounter, 0);
				}

				const float outerConeAngle = light.outerConeAngle;
				const float innerConeAngle = std::min(light.innerConeAngle, outerConeAngle);
				const float outerConeAngleCos = std::cos(outerConeAngle);
				const float innerConeAngleCos = std::cos(innerConeAngle);

				// https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual#inner-and-outer-cone-angles
				const float lightAngleScale = 1.0f / std::max(0.001f, innerConeAngleCos - outerConeAngleCos);
				const float lightAngleOffset = -outerConeAngleCos * lightAngleScale;

				shaderentity.SetConeAngleCos(outerConeAngleCos);
				shaderentity.SetAngleScale(lightAngleScale);
				shaderentity.SetAngleOffset(lightAngleOffset);

				if (shadow)
				{
					SHCAM shcam;
					CreateSpotLightShadowCam(light, shcam);
					XMStoreFloat4x4(&matrixArray[matrixCounter++], shcam.view_projection);
				}

				if (light.IsStatic())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				}

				if (light.IsVolumetricCloudsEnabled())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_VOLUMETRICCLOUDS);
				}

				std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
				entityCounter++;
				lightarray_count_spot++;
			}

			// Write point lights into entity array:
			lightarray_offset_point = entityCounter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entityCounter == SHADER_ENTITY_COUNT)
				{
					entityCounter--;
					break;
				}

				const LightComponent& light = vis.scene->lights[lightIndex];
				if (light.GetType() != LightComponent::POINT || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;

				Entity entity = vis.scene->lights.GetEntity(lightIndex);
				const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->layerMask;
				}

				shaderentity.SetType(light.GetType());
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.radius);
				shaderentity.SetLength(light.length);
				shaderentity.SetDirection(light.direction);
				shaderentity.SetColor(float4(light.color.x * light.intensity, light.color.y * light.intensity, light.color.z * light.intensity, 1));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				bool shadow = IsShadowsEnabled() && light.IsCastingShadow() && !light.IsStatic();
				const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];

				if (shadow)
				{
					shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
					shaderentity.SetIndices(matrixCounter, 0);
				}

				if (shadow)
				{
					const float FarZ = 0.1f;	// watch out: reversed depth buffer! Also, light near plane is constant for simplicity, this should match on cpu side!
					const float NearZ = std::max(1.0f, light.GetRange()); // watch out: reversed depth buffer!
					const float fRange = FarZ / (FarZ - NearZ);
					const float cubemapDepthRemapNear = fRange;
					const float cubemapDepthRemapFar = -fRange * NearZ;
					shaderentity.SetCubeRemapNear(cubemapDepthRemapNear);
					shaderentity.SetCubeRemapFar(cubemapDepthRemapFar);
				}

				if (light.IsStatic())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				}

				if (light.IsVolumetricCloudsEnabled())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_VOLUMETRICCLOUDS);
				}

				std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
				entityCounter++;
				lightarray_count_point++;
			}
			lightarray_count = lightarray_count_directional + lightarray_count_spot + lightarray_count_point;
			frameCB.entity_culling_count = lightarray_count + decalarray_count + envprobearray_count;

			// Write colliders into entity array:
			forcefieldarray_offset = entityCounter;
			for (size_t i = 0; i < vis.scene->collider_count_gpu; ++i)
			{
				if (entityCounter == SHADER_ENTITY_COUNT)
				{
					entityCounter--;
					break;
				}
				ShaderEntity shaderentity = {};

				const ColliderComponent& collider = vis.scene->colliders_gpu[i];
				shaderentity.layerMask = collider.layerMask;

				switch (collider.shape)
				{
				case ColliderComponent::Shape::Sphere:
					shaderentity.SetType(ENTITY_TYPE_COLLIDER_SPHERE);
					shaderentity.position = collider.sphere.center;
					shaderentity.SetRange(collider.sphere.radius);
					break;
				case ColliderComponent::Shape::Capsule:
					shaderentity.SetType(ENTITY_TYPE_COLLIDER_CAPSULE);
					shaderentity.position = collider.capsule.base;
					shaderentity.SetColliderTip(collider.capsule.tip);
					shaderentity.SetRange(collider.capsule.radius);
					break;
				case ColliderComponent::Shape::Plane:
					shaderentity.SetType(ENTITY_TYPE_COLLIDER_PLANE);
					shaderentity.position = collider.plane.origin;
					shaderentity.SetDirection(collider.plane.normal);
					shaderentity.SetIndices(matrixCounter, ~0u);
					matrixArray[matrixCounter++] = collider.plane.projection;
					break;
				default:
					assert(0);
					break;
				}

				std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
				entityCounter++;
				forcefieldarray_count++;
			}

			// Write force fields into entity array:
			for (size_t i = 0; i < vis.scene->forces.GetCount(); ++i)
			{
				if (entityCounter == SHADER_ENTITY_COUNT)
				{
					entityCounter--;
					break;
				}
				ShaderEntity shaderentity = {};

				const ForceFieldComponent& force = vis.scene->forces[i];

				shaderentity.layerMask = ~0u;

				Entity entity = vis.scene->forces.GetEntity(i);
				const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
				if (layer != nullptr)
				{
					shaderentity.layerMask = layer->layerMask;
				}

				switch (force.type)
				{
				default:
				case ForceFieldComponent::Type::Point:
					shaderentity.SetType(ENTITY_TYPE_FORCEFIELD_POINT);
					break;
				case ForceFieldComponent::Type::Plane:
					shaderentity.SetType(ENTITY_TYPE_FORCEFIELD_PLANE);
					break;
				}
				shaderentity.position = force.position;
				shaderentity.SetGravity(force.gravity);
				shaderentity.SetRange(std::max(0.001f, force.GetRange()));
				// The default planar force field is facing upwards, and thus the pull direction is downwards:
				shaderentity.SetDirection(force.direction);

				std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
				entityCounter++;
				forcefieldarray_count++;
			}
		}

		frameCB.probes = ShaderEntityIterator(envprobearray_offset, envprobearray_count);
		frameCB.directional_lights = ShaderEntityIterator(lightarray_offset_directional, lightarray_count_directional);
		frameCB.spotlights = ShaderEntityIterator(lightarray_offset_spot, lightarray_count_spot);
		frameCB.pointlights = ShaderEntityIterator(lightarray_offset_point, lightarray_count_point);
		frameCB.lights = ShaderEntityIterator(lightarray_offset, lightarray_count);
		frameCB.decals = ShaderEntityIterator(decalarray_offset, decalarray_count);
		frameCB.forces = ShaderEntityIterator(forcefieldarray_offset, forcefieldarray_count);
		/**/

	}


	const Sampler* GetSampler(SAMPLERTYPES id)
	{
		return &common::samplers[id];
	}
	const Shader* GetShader(SHADERTYPE id)
	{
		return &common::shaders[id];
	}
	const InputLayout* GetInputLayout(ILTYPES id)
	{
		return &common::inputLayouts[id];
	}
	const RasterizerState* GetRasterizerState(RSTYPES id)
	{
		return &common::rasterizers[id];
	}
	const DepthStencilState* GetDepthStencilState(DSSTYPES id)
	{
		return &common::depthStencils[id];
	}
	const BlendState* GetBlendState(BSTYPES id)
	{
		return &common::blendStates[id];
	}
	const GPUBuffer* GetBuffer(BUFFERTYPES id)
	{
		return &common::buffers[id];
	}
}

namespace vz
{
	struct GRenderPath3DDetails : GRenderPath3D
	{
		GRenderPath3DDetails(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
			: GRenderPath3D(vp, swapChain, rtRenderFinal) {}

		GraphicsDevice* device = nullptr;
		
		FrameCB frameCB;
		// separate graphics pipelines for the combination of special rendering effects
		renderer::View viewMain;

		renderer::View viewReflection;
		CameraComponent* cameraReflection = nullptr;

		// resources associated with render target buffers and textures

		void UpdateViewRes(const float dt);

		bool ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight) override; // must delete all canvas-related resources and re-create
		bool Render(const float dt) override;
		bool Destory() override;
	};

	bool GRenderPath3DDetails::ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight)
	{
		if (canvasWidth_ == canvasWidth || canvasHeight_ == canvasHeight)
		{
			return true;
		}

		canvasWidth_ = canvasWidth;
		canvasHeight_ = canvasHeight;
		return true;
	}

	void GRenderPath3DDetails::UpdateViewRes(const float dt)
	{
		// Frustum culling for main camera:
		renderer::View viewMain;
		viewMain.layerMask = ~0;
		viewMain.scene = scene;
		viewMain.camera = camera;
		viewMain.flags = renderer::View::ALLOW_EVERYTHING;
		if (!renderer::isOcclusionCullingEnabled)
		{
			viewMain.flags &= ~renderer::View::ALLOW_OCCLUSION_CULLING;
		}
		renderer::UpdateView(viewMain);

		// TODO
		if (viewMain.isPlanarReflectionVisible)
		{
			// Frustum culling for planar reflections:
			cameraReflection = camera;
			cameraReflection->jitter = XMFLOAT2(0, 0);
			//cameraReflection.Reflect(viewMain.reflectionPlane);
			//viewReflection.layerMask = getLayerMask();
			viewReflection.scene = scene;
			viewReflection.camera = cameraReflection;
			viewReflection.flags =
				//renderer::View::ALLOW_OBJECTS |
				//renderer::View::ALLOW_EMITTERS |
				//renderer::View::ALLOW_HAIRS |
				renderer::View::ALLOW_LIGHTS;
			renderer::UpdateView(viewReflection);
		}

		XMUINT2 internalResolution = XMUINT2(canvasWidth_, canvasHeight_);

		renderer::UpdatePerFrameData(
			*scene,
			viewMain,
			frameCB,
			renderer::isSceneUpdateEnabled ? dt : 0
		);

		/*
		if (getFSR2Enabled())
		{
			camera->jitter = fsr2Resources.GetJitter();
			camera_reflection.jitter.x = camera->jitter.x * 2;
			camera_reflection.jitter.y = camera->jitter.x * 2;
		}
		else if (renderer::GetTemporalAAEnabled())
		{
			const XMFLOAT4& halton = math::GetHaltonSequence(graphics::GetDevice()->GetFrameCount() % 256);
			camera->jitter.x = (halton.x * 2 - 1) / (float)internalResolution.x;
			camera->jitter.y = (halton.y * 2 - 1) / (float)internalResolution.y;
			camera_reflection.jitter.x = camera->jitter.x * 2;
			camera_reflection.jitter.y = camera->jitter.x * 2;
			if (!temporalAAResources.IsValid())
			{
				renderer::CreateTemporalAAResources(temporalAAResources, internalResolution);
			}
		}
		else
		{
			camera->jitter = XMFLOAT2(0, 0);
			camera_reflection.jitter = XMFLOAT2(0, 0);
			temporalAAResources = {};
		}

		camera->UpdateCamera();
		if (viewMain.planar_reflection_visible)
		{
			camera_reflection.UpdateCamera();
		}

		if (getAO() != AO_RTAO)
		{
			rtaoResources.frame = 0;
		}
		if (!renderer::GetRaytracedShadowsEnabled())
		{
			rtshadowResources.frame = 0;
		}
		if (!getSSREnabled() && !getRaytracedReflectionEnabled())
		{
			rtSSR = {};
		}
		if (!getSSGIEnabled())
		{
			rtSSGI = {};
		}
		if (!getRaytracedDiffuseEnabled())
		{
			rtRaytracedDiffuse = {};
		}
		if (getAO() == AO_DISABLED)
		{
			rtAO = {};
		}

		if (renderer::GetRaytracedShadowsEnabled() && device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
		{
			if (!rtshadowResources.denoised.IsValid())
			{
				renderer::CreateRTShadowResources(rtshadowResources, internalResolution);
			}
		}
		else
		{
			rtshadowResources = {};
		}

		if (scene->weather.IsRealisticSky() && scene->weather.IsRealisticSkyAerialPerspective())
		{
			if (!aerialperspectiveResources.texture_output.IsValid())
			{
				renderer::CreateAerialPerspectiveResources(aerialperspectiveResources, internalResolution);
			}
			if (getReflectionsEnabled() && depthBuffer_Reflection.IsValid())
			{
				if (!aerialperspectiveResources_reflection.texture_output.IsValid())
				{
					renderer::CreateAerialPerspectiveResources(aerialperspectiveResources_reflection, XMUINT2(depthBuffer_Reflection.desc.width, depthBuffer_Reflection.desc.height));
				}
			}
			else
			{
				aerialperspectiveResources_reflection = {};
			}
		}
		else
		{
			aerialperspectiveResources = {};
		}		

		if (!scene->waterRipples.empty())
		{
			if (!rtWaterRipple.IsValid())
			{
				TextureDesc desc;
				desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
				desc.format = Format::R16G16_FLOAT;
				desc.width = internalResolution.x / 8;
				desc.height = internalResolution.y / 8;
				assert(ComputeTextureMemorySizeInBytes(desc) <= ComputeTextureMemorySizeInBytes(rtParticleDistortion.desc)); // aliasing check
				device->CreateTexture(&desc, nullptr, &rtWaterRipple, &rtParticleDistortion); // aliased!
				device->SetName(&rtWaterRipple, "rtWaterRipple");
			}
		}
		else
		{
			rtWaterRipple = {};
		}

		if (renderer::GetSurfelGIEnabled())
		{
			if (!surfelGIResources.result.IsValid())
			{
				renderer::CreateSurfelGIResources(surfelGIResources, internalResolution);
			}
		}

		if (renderer::GetVXGIEnabled())
		{
			if (!vxgiResources.IsValid())
			{
				renderer::CreateVXGIResources(vxgiResources, internalResolution);
			}
		}
		else
		{
			vxgiResources = {};
		}

		// Check whether reprojected depth is required:
		if (!first_frame && renderer::IsMeshShaderAllowed() && renderer::IsMeshletOcclusionCullingEnabled())
		{
			TextureDesc desc;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.format = Format::R16_UNORM;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			desc.mip_levels = GetMipCount(desc.width, desc.height, 1, 4);
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			device->CreateTexture(&desc, nullptr, &reprojectedDepth);
			device->SetName(&reprojectedDepth, "reprojectedDepth");

			for (uint32_t i = 0; i < reprojectedDepth.desc.mip_levels; ++i)
			{
				int subresource_index;
				subresource_index = device->CreateSubresource(&reprojectedDepth, SubresourceType::SRV, 0, 1, i, 1);
				assert(subresource_index == i);
				subresource_index = device->CreateSubresource(&reprojectedDepth, SubresourceType::UAV, 0, 1, i, 1);
				assert(subresource_index == i);
			}
		}
		else
		{
			reprojectedDepth = {};
		}

		// Check whether velocity buffer is required:
		if (
			getMotionBlurEnabled() ||
			renderer::GetTemporalAAEnabled() ||
			getSSREnabled() ||
			getSSGIEnabled() ||
			getRaytracedReflectionEnabled() ||
			getRaytracedDiffuseEnabled() ||
			renderer::GetRaytracedShadowsEnabled() ||
			getAO() == AO::AO_RTAO ||
			renderer::GetVariableRateShadingClassification() ||
			getFSR2Enabled() ||
			reprojectedDepth.IsValid()
			)
		{
			if (!rtVelocity.IsValid())
			{
				TextureDesc desc;
				desc.format = Format::R16G16_FLOAT;
				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS | BindFlag::RENDER_TARGET;
				desc.width = internalResolution.x;
				desc.height = internalResolution.y;
				desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
				device->CreateTexture(&desc, nullptr, &rtVelocity);
				device->SetName(&rtVelocity, "rtVelocity");
			}
		}
		else
		{
			rtVelocity = {};
		}

		// Check whether shadow mask is required:
		if (renderer::GetScreenSpaceShadowsEnabled() || renderer::GetRaytracedShadowsEnabled())
		{
			if (!rtShadow.IsValid())
			{
				TextureDesc desc;
				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.format = Format::R8_UNORM;
				desc.array_size = 16;
				desc.width = internalResolution.x;
				desc.height = internalResolution.y;
				desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
				device->CreateTexture(&desc, nullptr, &rtShadow);
				device->SetName(&rtShadow, "rtShadow");
			}
		}
		else
		{
			rtShadow = {};
		}

		if (getFSR2Enabled())
		{
			// FSR2 also acts as a temporal AA, so we inform the shaders about it here
			//	This will allow improved stochastic alpha test transparency
			frameCB.options |= OPTION_BIT_TEMPORALAA_ENABLED;
			uint x = frameCB.frame_count % 4;
			uint y = frameCB.frame_count / 4;
			frameCB.temporalaa_samplerotation = (x & 0x000000FF) | ((y & 0x000000FF) << 8);
		}

		// Check whether visibility resources are required:
		if (
			visibility_shading_in_compute ||
			getSSREnabled() ||
			getSSGIEnabled() ||
			getRaytracedReflectionEnabled() ||
			getRaytracedDiffuseEnabled() ||
			renderer::GetScreenSpaceShadowsEnabled() ||
			renderer::GetRaytracedShadowsEnabled() ||
			renderer::GetVXGIEnabled()
			)
		{
			if (!visibilityResources.IsValid())
			{
				renderer::CreateVisibilityResources(visibilityResources, internalResolution);
			}
		}
		else
		{
			visibilityResources = {};
		}

		// Check for depth of field allocation:
		if (getDepthOfFieldEnabled() &&
			getDepthOfFieldStrength() > 0 &&
			camera->aperture_size > 0
			)
		{
			if (!depthoffieldResources.IsValid())
			{
				XMUINT2 resolution = GetInternalResolution();
				if (getFSR2Enabled())
				{
					resolution = XMUINT2(GetPhysicalWidth(), GetPhysicalHeight());
				}
				renderer::CreateDepthOfFieldResources(depthoffieldResources, resolution);
			}
		}
		else
		{
			depthoffieldResources = {};
		}

		// Check for motion blur allocation:
		if (getMotionBlurEnabled() && getMotionBlurStrength() > 0)
		{
			if (!motionblurResources.IsValid())
			{
				XMUINT2 resolution = GetInternalResolution();
				if (getFSR2Enabled())
				{
					resolution = XMUINT2(GetPhysicalWidth(), GetPhysicalHeight());
				}
				renderer::CreateMotionBlurResources(motionblurResources, resolution);
			}
		}
		else
		{
			motionblurResources = {};
		}

		// Keep a copy of last frame's depth buffer for temporal disocclusion checks, so swap with current one every frame:
		std::swap(depthBuffer_Copy, depthBuffer_Copy1);

		visibilityResources.depthbuffer = &depthBuffer_Copy;
		visibilityResources.lineardepth = &rtLinearDepth;
		if (getMSAASampleCount() > 1)
		{
			visibilityResources.primitiveID_resolved = &rtPrimitiveID;
		}
		else
		{
			visibilityResources.primitiveID_resolved = nullptr;
		}
		/**/
	}

	bool GRenderPath3DDetails::Render(const float dt)
	{
		device = GetDevice();

		UpdateViewRes(dt);

		CommandList cmd = device->BeginCommandList();
		//RenderPassImage rp[] = {
		//	RenderPassImage::RenderTarget(&rtOut, RenderPassImage::LoadOp::CLEAR),
		//
		//};
		//graphicsDevice_->RenderPassBegin(rp, arraysize(rp), cmd);
		device->RenderPassBegin(&swapChain_, cmd);
		device->RenderPassEnd(cmd);
		device->SubmitCommandLists();

		return true;
	}

	bool GRenderPath3DDetails::Destory()
	{
		return true;
	}

}

namespace vz
{
	GRenderPath3D* NewGRenderPath(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
	{
		return new GRenderPath3DDetails(vp, swapChain, rtRenderFinal);
	}

	bool InitRendererShaders()
	{
		Timer timer;

		initializer::SetUpStates();
		initializer::LoadBuffers();

		//static eventhandler::Handle handle2 = eventhandler::Subscribe(eventhandler::EVENT_RELOAD_SHADERS, [](uint64_t userdata) { LoadShaders(); });
		//LoadShaders();

		backlog::post("renderer Initialized (" + std::to_string((int)std::round(timer.elapsed())) + " ms)", backlog::LogLevel::Info);
		//initialized.store(true);
		return true;
	}

}

