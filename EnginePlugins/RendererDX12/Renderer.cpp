#include "Renderer.h"
#include "Image.h"
#include "TextureHelper.h"

#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"
#include "Utils/Spinlock.h"
#include "Utils/Profiler.h"
#include "Utils/Helpers.h"
#include "Libs/Math.h"
#include "Libs/Geometrics.h"

#include "ThirdParty/RectPacker.h"

namespace vz::rcommon
{
	InputLayout			inputLayouts[ILTYPE_COUNT];
	RasterizerState		rasterizers[RSTYPE_COUNT];
	DepthStencilState	depthStencils[DSSTYPE_COUNT];
	BlendState			blendStates[BSTYPE_COUNT];
	Shader				shaders[SHADERTYPE_COUNT];
	GPUBuffer			buffers[BUFFERTYPE_COUNT];
	Sampler				samplers[SAMPLER_COUNT];
	Texture				textures[TEXTYPE_COUNT];

	GPUBuffer			luminanceDummy;

	PipelineState		PSO_debug[DEBUGRENDERING_COUNT];
	PipelineState		PSO_wireframe;
	PipelineState		PSO_occlusionquery;
	std::unordered_map<uint32_t, PipelineState> PSO_render[RENDERPASS_COUNT][SHADERTYPE_BIN_COUNT];

	jobsystem::context	CTX_renderPSO[RENDERPASS_COUNT][MESH_SHADER_PSO_COUNT];

	// progressive components
	std::vector<std::pair<Texture, bool>> deferredMIPGens;
	std::vector<std::pair<Texture, Texture>> deferredBCQueue; // BC : Block Compression
	std::vector<std::pair<Texture, Texture>> deferredTextureCopy;
	std::vector<std::pair<GPUBuffer, std::pair<void*, size_t>>> deferredBufferUpdate;
	SpinLock deferredResourceLock;
}

// TODO 
// move 
//	1. renderer options and functions to GRenderPath3DDetails
//	2. global parameters to vz::rcommon

namespace vz::renderer
{
	float giBoost = 1.f;
	float renderingSpeed = 1.f;
	bool isOcclusionCullingEnabled = true;
	bool isWetmapRefreshEnabled = false;
	bool isFreezeCullingCameraEnabled = false;
	bool isSceneUpdateEnabled = true;
	bool isTemporalAAEnabled = false;
	bool isTessellationEnabled = false;
	bool isFSREnabled = false;
	bool isWireRender = false;
	bool isDebugLightCulling = false;
	bool isAdvancedLightCulling = false;
	bool isMeshShaderAllowed = false;
	bool isShadowsEnabled = false;
	bool isVariableRateShadingClassification = false;
	bool isSurfelGIDebugEnabled = false;
	bool isColorGradingEnabled = false;

	namespace options
	{
		void SetOcclusionCullingEnabled(bool enabled) { isOcclusionCullingEnabled = enabled; }
		bool IsOcclusionCullingEnabled() { return isOcclusionCullingEnabled; }
		void SetFreezeCullingCameraEnabled(bool enabled) { isFreezeCullingCameraEnabled = enabled; }
		bool IsFreezeCullingCameraEnabled() { return isFreezeCullingCameraEnabled; }
	}


	using namespace geometrics;

	struct View
	{
		// User fills these:
		uint8_t layerMask = ~0;
		Scene* scene = nullptr;
		CameraComponent* camera = nullptr;
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

		Frustum frustum; // camera's frustum or special purposed frustum
		std::vector<uint32_t> visibleRenderables; // index refers to the linear array of Scnee::renderables

		// TODO: visibleRenderables into visibleMeshRenderables and visibleVolumeRenderables
		//	and use them instead of visibleRenderables
		std::vector<uint32_t> visibleMeshRenderables;
		std::vector<uint32_t> visibleVolumeRenderables;

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
		const graphics::Texture* primitiveID_1_resolved = nullptr; // resolved from MSAA texture_visibility input
		const graphics::Texture* primitiveID_2_resolved = nullptr; // resolved from MSAA texture_visibility input

		inline bool IsValid() const { return bins.IsValid(); }
	};

	// must be called after scene->update()
	void UpdateView(View& view)
	{
		// Perform parallel frustum culling and obtain closest reflector:
		jobsystem::context ctx;
		auto range = profiler::BeginRangeCPU("Frustum Culling");

		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		assert(view.scene != nullptr); // User must provide a scene!
		assert(view.camera != nullptr); // User must provide a camera!

		// The parallel frustum culling is first performed in shared memory, 
		//	then each group writes out it's local list to global memory
		//	The shared memory approach reduces atomics and helps the list to remain
		//	more coherent (less randomly organized compared to original order)
		static const uint32_t groupSize = 64;
		static const size_t sharedmemory_size = (groupSize + 1) * sizeof(uint32_t); // list + counter per group

		// Initialize visible indices:
		view.Clear();

		// TODO : add frustum culling processs
		if (!isFreezeCullingCameraEnabled) // just for debug
		{
			view.frustum = view.camera->GetFrustum();
		}
		if (!isOcclusionCullingEnabled || isFreezeCullingCameraEnabled)
		{
			view.flags &= ~View::ALLOW_OCCLUSION_CULLING;
		}
		
		if (view.flags & View::ALLOW_LIGHTS)
		{
			// Cull lights:
			const uint32_t light_loop = (uint32_t)view.scene->GetLightCount();
			view.visibleLights.resize(light_loop);
			//vis.visibleLightShadowRects.clear();
			//vis.visibleLightShadowRects.resize(light_loop);
			jobsystem::Dispatch(ctx, light_loop, groupSize, [&](jobsystem::JobArgs args) {

				const std::vector<Entity>& light_entities = view.scene->GetLightEntities();
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

				if ((aabb.layerMask & view.layerMask) && view.frustum.CheckBoxFast(aabb))
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
					uint32_t prev_count = view.lightCounter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						view.visibleLights[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		if (view.flags & View::ALLOW_RENDERABLES)
		{
			// Cull objects:
			const uint32_t renderable_loop = (uint32_t)view.scene->GetRenderableCount();
			view.visibleRenderables.resize(renderable_loop);
			jobsystem::Dispatch(ctx, renderable_loop, groupSize, [&](jobsystem::JobArgs args) {

				const RenderableComponent& renderable = *scene_Gdetails->renderableComponents[args.jobIndex];
				//assert(!renderable.IsDirty());

				// Setup stream compaction:
				uint32_t& group_count = *(uint32_t*)args.sharedmemory;
				uint32_t* group_list = (uint32_t*)args.sharedmemory + 1;
				if (args.isFirstJobInGroup)
				{
					group_count = 0; // first thread initializes local counter
				}

				const AABB& aabb = renderable.GetAABB();

				if ((aabb.layerMask & view.layerMask) && view.frustum.CheckBoxFast(aabb))
				{
					// Local stream compaction:
					group_list[group_count++] = args.jobIndex;

					GSceneDetails::OcclusionResult& occlusion_result = scene_Gdetails->occlusionResultsObjects[args.jobIndex];
					bool occluded = false;
					if (view.flags & View::ALLOW_OCCLUSION_CULLING)
					{
						occluded = occlusion_result.IsOccluded();
					}

					if (view.flags & View::ALLOW_OCCLUSION_CULLING)
					{
						if (renderable.IsMeshRenderable() && occlusion_result.occlusionQueries[scene_Gdetails->queryheapIdx] < 0)
						{
							if (aabb.intersects(view.camera->GetWorldEye()))
							{
								// camera is inside the instance, mark it as visible in this frame:
								occlusion_result.occlusionHistory |= 1;
							}
							else
							{
								occlusion_result.occlusionQueries[scene_Gdetails->queryheapIdx] = scene_Gdetails->queryAllocator.fetch_add(1); // allocate new occlusion query from heap
							}
						}
					}
				}

				// Global stream compaction:
				if (args.isLastJobInGroup && group_count > 0)
				{
					uint32_t prev_count = view.renderableCounter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						view.visibleRenderables[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		jobsystem::Wait(ctx);

		// finalize stream compaction: (memory safe)
		view.visibleRenderables.resize((size_t)view.renderableCounter.load());
		view.visibleLights.resize((size_t)view.lightCounter.load());
		
		profiler::EndRange(range); // Frustum Culling
	}
	void UpdatePerFrameData(Scene& scene, const View& vis, FrameCB& frameCB, float dt)
	{
		GraphicsDevice* device = graphics::GetDevice();
		GSceneDetails* scene_Gdetails = (GSceneDetails*)scene.GetGSceneHandle();

		// Update CPU-side frame constant buffer:
		frameCB.Init();
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

		frameCB.gi_boost = giBoost;

		frameCB.options = 0;
		if (isTemporalAAEnabled)
		{
			frameCB.options |= OPTION_BIT_TEMPORALAA_ENABLED;
		}
		//frameCB.options |= OPTION_BIT_DISABLE_ALBEDO_MAPS;
		frameCB.options |= OPTION_BIT_FORCE_DIFFUSE_LIGHTING;

		frameCB.scene = scene_Gdetails->shaderscene;

		frameCB.texture_random64x64_index = device->GetDescriptorIndex(texturehelper::getRandom64x64(), SubresourceType::SRV);
		frameCB.texture_bluenoise_index = device->GetDescriptorIndex(texturehelper::getBlueNoise(), SubresourceType::SRV);
		frameCB.texture_sheenlut_index = device->GetDescriptorIndex(&rcommon::textures[TEXTYPE_2D_SHEENLUT], SubresourceType::SRV);

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
			ShaderEntity* entity_array = frameCB.entityArray;
			float4x4* light_matrix_array = frameCB.matrixArray;

			uint32_t entity_counter = 0;
			uint32_t matrix_counter = 0;

			// Write decals into entity array:
			//decalarray_offset = entityCounter;
			//const size_t decal_iterations = std::min((size_t)MAX_SHADER_DECAL_COUNT, vis.visibleDecals.size());
			//for (size_t i = 0; i < decal_iterations; ++i)
			//{
			//	if (entity_counter == SHADER_ENTITY_COUNT)
			//	{
			//		backlog::post("Shader Entity Overflow!! >> DECALS");
			//		entity_counter--;
			//		break;
			//	}
			//	if (matrix_counter >= MATRIXARRAY_COUNT)
			//	{
			//		matrix_counter--;
			//		break;
			//	}
			//	ShaderEntity shaderentity = {};
			//	XMMATRIX shadermatrix;
			//
			//	const uint32_t decalIndex = vis.visibleDecals[vis.visibleDecals.size() - 1 - i]; // note: reverse order, for correct blending!
			//	const DecalComponent& decal = vis.scene->decals[decalIndex];
			//
			//	shaderentity.layerMask = ~0u;
			//
			//	Entity entity = vis.scene->decals.GetEntity(decalIndex);
			//	const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
			//	if (layer != nullptr)
			//	{
			//		shaderentity.layerMask = layer->layerMask;
			//	}
			//
			//	shaderentity.SetType(ENTITY_TYPE_DECAL);
			//	if (decal.IsBaseColorOnlyAlpha())
			//	{
			//		shaderentity.SetFlags(ENTITY_FLAG_DECAL_BASECOLOR_ONLY_ALPHA);
			//	}
			//	shaderentity.position = decal.position;
			//	shaderentity.SetRange(decal.range);
			//	float emissive_mul = 1 + decal.emissive;
			//	shaderentity.SetColor(float4(decal.color.x * emissive_mul, decal.color.y * emissive_mul, decal.color.z * emissive_mul, decal.color.w));
			//	shaderentity.shadowAtlasMulAdd = decal.texMulAdd;
			//	shaderentity.SetConeAngleCos(decal.slopeBlendPower);
			//	shaderentity.SetDirection(decal.front);
			//	shaderentity.SetAngleScale(decal.normal_strength);
			//	shaderentity.SetLength(decal.displacement_strength);
			//
			//	shaderentity.SetIndices(matrixCounter, 0);
			//	shadermatrix = XMMatrixInverse(nullptr, XMLoadFloat4x4(&decal.world));
			//
			//	int texture = -1;
			//	if (decal.texture.IsValid())
			//	{
			//		texture = device->GetDescriptorIndex(&decal.texture.GetTexture(), SubresourceType::SRV, decal.texture.GetTextureSRGBSubresource());
			//	}
			//	int normal = -1;
			//	if (decal.normal.IsValid())
			//	{
			//		normal = device->GetDescriptorIndex(&decal.normal.GetTexture(), SubresourceType::SRV);
			//	}
			//	int surfacemap = -1;
			//	if (decal.surfacemap.IsValid())
			//	{
			//		surfacemap = device->GetDescriptorIndex(&decal.surfacemap.GetTexture(), SubresourceType::SRV);
			//	}
			//	int displacementmap = -1;
			//	if (decal.displacementmap.IsValid())
			//	{
			//		displacementmap = device->GetDescriptorIndex(&decal.displacementmap.GetTexture(), SubresourceType::SRV);
			//	}
			//
			//	shadermatrix.r[0] = XMVectorSetW(shadermatrix.r[0], *(float*)&texture);
			//	shadermatrix.r[1] = XMVectorSetW(shadermatrix.r[1], *(float*)&normal);
			//	shadermatrix.r[2] = XMVectorSetW(shadermatrix.r[2], *(float*)&surfacemap);
			//	shadermatrix.r[3] = XMVectorSetW(shadermatrix.r[3], *(float*)&displacementmap);
			//
			//	XMStoreFloat4x4(matrixArray + matrixCounter, shadermatrix);
			//	matrixCounter++;
			//
			//	std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
			//	entityCounter++;
			//	decalarray_count++;
			//}

			// Write environment probes into entity array:
			//envprobearray_offset = entityCounter;
			//const size_t probe_iterations = std::min((size_t)MAX_SHADER_PROBE_COUNT, vis.visibleEnvProbes.size());
			//for (size_t i = 0; i < probe_iterations; ++i)
			//{
			//	if (entity_counter == SHADER_ENTITY_COUNT)
			//	{
			//		backlog::post("Shader Entity Overflow!! >> LIGHT PROBES");
			//		entity_counter--;
			//		break;
			//	}
			//	if (matrix_counter >= MATRIXARRAY_COUNT)
			//	{
			//		matrix_counter--;
			//		break;
			//	}
			//	ShaderEntity shaderentity = {};
			//	XMMATRIX shadermatrix;
			//
			//	const uint32_t probeIndex = vis.visibleEnvProbes[vis.visibleEnvProbes.size() - 1 - i]; // note: reverse order, for correct blending!
			//	const EnvironmentProbeComponent& probe = vis.scene->probes[probeIndex];
			//
			//	shaderentity = {}; // zero out!
			//	shaderentity.layerMask = ~0u;
			//
			//	Entity entity = vis.scene->probes.GetEntity(probeIndex);
			//	const LayerComponent* layer = vis.scene->layers.GetComponent(entity);
			//	if (layer != nullptr)
			//	{
			//		shaderentity.layerMask = layer->layerMask;
			//	}
			//
			//	shaderentity.SetType(ENTITY_TYPE_ENVMAP);
			//	shaderentity.position = probe.position;
			//	shaderentity.SetRange(probe.range);
			//
			//	shaderentity.SetIndices(matrixCounter, 0);
			//	shadermatrix = XMLoadFloat4x4(&probe.inverseMatrix);
			//
			//	int texture = -1;
			//	if (probe.texture.IsValid())
			//	{
			//		texture = device->GetDescriptorIndex(&probe.texture, SubresourceType::SRV);
			//	}
			//
			//	shadermatrix.r[0] = XMVectorSetW(shadermatrix.r[0], *(float*)&texture);
			//	shadermatrix.r[1] = XMVectorSetW(shadermatrix.r[1], 0);
			//	shadermatrix.r[2] = XMVectorSetW(shadermatrix.r[2], 0);
			//	shadermatrix.r[3] = XMVectorSetW(shadermatrix.r[3], 0);
			//
			//	XMStoreFloat4x4(matrixArray + matrixCounter, shadermatrix);
			//	matrixCounter++;
			//
			//	std::memcpy(entityArray + entityCounter, &shaderentity, sizeof(ShaderEntity));
			//	entityCounter++;
			//	envprobearray_count++;
			//}
			
			//const XMFLOAT2 atlas_dim_rcp = XMFLOAT2(1.0f / float(shadowMapAtlas.desc.width), 1.0f / float(shadowMapAtlas.desc.height));

			const std::vector<Entity>& light_entities = vis.scene->GetLightEntities();
			// Write directional lights into entity array:
			lightarray_offset = entity_counter;
			lightarray_offset_directional = entity_counter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entity_counter == SHADER_ENTITY_COUNT)
				{
					backlog::post("Shader Entity Overflow!! >> Directional Light");
					entity_counter--;
					break;
				}

				const GLightComponent& light = *(GLightComponent*)compfactory::GetLightComponent(light_entities[lightIndex]);
				if (light.GetLightType() != LightComponent::LightType::DIRECTIONAL || light.IsInactive())
					continue;

				ShaderEntity shaderentity = {};
				shaderentity.layerMask = ~0u;

				shaderentity.SetType(SCU32(light.GetLightType()));
				shaderentity.position = light.position;
				shaderentity.SetRange(light.GetRange());
				shaderentity.SetRadius(light.GetRadius());
				shaderentity.SetLength(light.GetLength());
				// note: the light direction used in shader refers to the direction to the light source
				shaderentity.SetDirection(XMFLOAT3(-light.direction.x, -light.direction.y, -light.direction.z));
				XMFLOAT3 light_color = light.GetLightColor();
				float light_intensity = light.GetLightIntensity();
				shaderentity.SetColor(float4(light_color.x * light_intensity, light_color.y * light_intensity, light_color.z * light_intensity, 1.f));

				// mark as no shadow by default:
				shaderentity.indices = ~0;

				bool shadow = false;// IsShadowsEnabled() && light.IsCastingShadow() && !light.IsStatic();
				//const rectpacker::Rect& shadow_rect = vis.visibleLightShadowRects[lightIndex];
				if (shadow)
				{
					//shaderentity.shadowAtlasMulAdd.x = shadow_rect.w * atlas_dim_rcp.x;
					//shaderentity.shadowAtlasMulAdd.y = shadow_rect.h * atlas_dim_rcp.y;
					//shaderentity.shadowAtlasMulAdd.z = shadow_rect.x * atlas_dim_rcp.x;
					//shaderentity.shadowAtlasMulAdd.w = shadow_rect.y * atlas_dim_rcp.y;
					//shaderentity.SetIndices(matrixCounter, 0);
				}

				const uint cascade_count = std::min((uint)light.cascadeDistances.size(), SHADER_ENTITY_COUNT - matrix_counter);
				shaderentity.SetShadowCascadeCount(cascade_count);

				//if (shadow && !light.cascade_distances.empty())
				//{
				//	SHCAM* shcams = (SHCAM*)alloca(sizeof(SHCAM) * cascade_count);
				//	CreateDirLightShadowCams(light, *vis.camera, shcams, cascade_count, shadow_rect);
				//	for (size_t cascade = 0; cascade < cascade_count; ++cascade)
				//	{
				//		XMStoreFloat4x4(&light_matrix_array[light_matrix_counter++], shcams[cascade].view_projection);
				//	}
				//}

				//if (light.IsStatic())
				//{
				//	shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				//}

				std::memcpy(entity_array + entity_counter, &shaderentity, sizeof(ShaderEntity));
				entity_counter++;
				lightarray_count_directional++;
			}


			/*
			// Write spot lights into entity array:
			lightarray_offset_spot = entity_counter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entity_counter == SHADER_ENTITY_COUNT)
				{
					entity_counter--;
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
				// note: the light direction used in shader refers to the direction to the light source
				shaderentity.SetDirection(XMFLOAT3(-light.direction.x, -light.direction.y, -light.direction.z));
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
					shaderentity.SetIndices(matrix_counter, 0);
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
					XMStoreFloat4x4(&matrixArray[matrix_counter++], shcam.view_projection);
				}

				if (light.IsStatic())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_STATIC);
				}

				if (light.IsVolumetricCloudsEnabled())
				{
					shaderentity.SetFlags(ENTITY_FLAG_LIGHT_VOLUMETRICCLOUDS);
				}

				std::memcpy(entityArray + entity_counter, &shaderentity, sizeof(ShaderEntity));
				entity_counter++;
				lightarray_count_spot++;
			}

			// Write point lights into entity array:
			lightarray_offset_point = entity_counter;
			for (uint32_t lightIndex : vis.visibleLights)
			{
				if (entity_counter == SHADER_ENTITY_COUNT)
				{
					entity_counter--;
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
				// note: the light direction used in shader refers to the direction to the light source
				shaderentity.SetDirection(XMFLOAT3(-light.direction.x, -light.direction.y, -light.direction.z));
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
					shaderentity.SetIndices(matrix_counter, 0);
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

				std::memcpy(entityArray + entity_counter, &shaderentity, sizeof(ShaderEntity));
				entity_counter++;
				lightarray_count_point++;
			}
			/**/

			lightarray_count = lightarray_count_directional + lightarray_count_spot + lightarray_count_point;
			frameCB.entity_culling_count = lightarray_count + decalarray_count + envprobearray_count;

			/*
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
			/**/

		}



		frameCB.probes = ShaderEntityIterator(envprobearray_offset, envprobearray_count);
		frameCB.directional_lights = ShaderEntityIterator(lightarray_offset_directional, lightarray_count_directional);
		//frameCB.spotlights = ShaderEntityIterator(lightarray_offset_spot, lightarray_count_spot);
		//frameCB.pointlights = ShaderEntityIterator(lightarray_offset_point, lightarray_count_point);
		frameCB.lights = ShaderEntityIterator(lightarray_offset, lightarray_count);
		frameCB.decals = ShaderEntityIterator(decalarray_offset, decalarray_count);
		//frameCB.forces = ShaderEntityIterator(forcefieldarray_offset, forcefieldarray_count);
	}

	constexpr uint32_t CombineStencilrefs(StencilRef engineStencilRef, uint8_t userStencilRef)
	{
		return (userStencilRef << 4) | static_cast<uint8_t>(engineStencilRef);
	}
	constexpr XMUINT2 GetViewTileCount(XMUINT2 internalResolution)
	{
		return XMUINT2(
			(internalResolution.x + VISIBILITY_BLOCKSIZE - 1) / VISIBILITY_BLOCKSIZE,
			(internalResolution.y + VISIBILITY_BLOCKSIZE - 1) / VISIBILITY_BLOCKSIZE
		);
	}
	constexpr XMUINT2 GetEntityCullingTileCount(XMUINT2 internalResolution)
	{
		return XMUINT2(
			(internalResolution.x + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE,
			(internalResolution.y + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE
		);
	}


	const Sampler* GetSampler(SAMPLERTYPES id)
	{
		return &rcommon::samplers[id];
	}
	const Shader* GetShader(SHADERTYPE id)
	{
		return &rcommon::shaders[id];
	}
	const InputLayout* GetInputLayout(ILTYPES id)
	{
		return &rcommon::inputLayouts[id];
	}
	const RasterizerState* GetRasterizerState(RSTYPES id)
	{
		return &rcommon::rasterizers[id];
	}
	const DepthStencilState* GetDepthStencilState(DSSTYPES id)
	{
		return &rcommon::depthStencils[id];
	}
	const BlendState* GetBlendState(BSTYPES id)
	{
		return &rcommon::blendStates[id];
	}
	const GPUBuffer* GetBuffer(BUFFERTYPES id)
	{
		return &rcommon::buffers[id];
	}
	
	enum MIPGENFILTER
	{
		MIPGENFILTER_POINT,
		MIPGENFILTER_LINEAR,
		MIPGENFILTER_GAUSSIAN,
	};
	enum DRAWSCENE_FLAGS
	{
		DRAWSCENE_OPAQUE = 1 << 0, // include opaque objects
		DRAWSCENE_TRANSPARENT = 1 << 1, // include transparent objects
		DRAWSCENE_OCCLUSIONCULLING = 1 << 2, // enable skipping objects based on occlusion culling results
		DRAWSCENE_TESSELLATION = 1 << 3, // enable tessellation
		DRAWSCENE_FOREGROUND_ONLY = 1 << 4, // only include objects that are tagged as foreground
	};

	struct MIPGEN_OPTIONS
	{
		int arrayIndex = -1;
		const graphics::Texture* gaussian_temp = nullptr;
		bool preserve_coverage = false;
		bool wide_gauss = false;
	};

	struct TemporalAAResources
	{
		mutable int frame = 0;
		graphics::Texture textureTemporal[2];

		bool IsValid() const { return textureTemporal[0].IsValid(); }
		const graphics::Texture* GetCurrent() const { return &textureTemporal[frame % arraysize(textureTemporal)]; }
		const graphics::Texture* GetHistory() const { return &textureTemporal[(frame + 1) % arraysize(textureTemporal)]; }
	};
	struct TiledLightResources
	{
		XMUINT2 tileCount = {};
		graphics::GPUBuffer entityTiles; // culled entity indices
	};
	struct LuminanceResources
	{
		graphics::GPUBuffer luminance;
	};
	struct BloomResources
	{
		graphics::Texture texture_bloom;
		graphics::Texture texture_temp;
	};
	// Direct reference to a renderable instance:
	struct RenderBatch
	{
		uint32_t geometryIndex;
		uint32_t instanceIndex;	// renderable index
		uint16_t distance;
		uint16_t camera_mask;
		uint32_t sort_bits; // an additional bitmask for sorting only, it should be used to reduce pipeline changes

		inline void Create(uint32_t renderableIndex, uint32_t instanceIndex, float distance, uint32_t sort_bits, uint16_t camera_mask = 0xFFFF)
		{
			this->geometryIndex = renderableIndex;
			this->instanceIndex = instanceIndex;
			this->distance = XMConvertFloatToHalf(distance);
			this->sort_bits = sort_bits;
			this->camera_mask = camera_mask;
		}

		inline float GetDistance() const
		{
			return XMConvertHalfToFloat(HALF(distance));
		}
		constexpr uint32_t GetGeometryIndex() const
		{
			return geometryIndex;
		}
		constexpr uint32_t GetRenderableIndex() const
		{
			return instanceIndex;
		}

		// opaque sorting
		//	Priority is set to mesh index to have more instancing
		//	distance is second priority (front to back Z-buffering)
		constexpr bool operator<(const RenderBatch& other) const
		{
			union SortKey
			{
				struct
				{
					// The order of members is important here, it means the sort priority (low to high)!
					uint64_t distance : 16;
					uint64_t meshIndex : 16;
					uint64_t sort_bits : 32;
				} bits;
				uint64_t value;
			};
			static_assert(sizeof(SortKey) == sizeof(uint64_t));
			SortKey a = {};
			a.bits.distance = distance;
			a.bits.meshIndex = geometryIndex;
			a.bits.sort_bits = sort_bits;
			SortKey b = {};
			b.bits.distance = other.distance;
			b.bits.meshIndex = other.geometryIndex;
			b.bits.sort_bits = other.sort_bits;
			return a.value < b.value;
		}
		// transparent sorting
		//	Priority is distance for correct alpha blending (back to front rendering)
		//	mesh index is second priority for instancing
		constexpr bool operator>(const RenderBatch& other) const
		{
			union SortKey
			{
				struct
				{
					// The order of members is important here, it means the sort priority (low to high)!
					uint64_t meshIndex : 16;
					uint64_t sort_bits : 32;
					uint64_t distance : 16;
				} bits;
				uint64_t value;
			};
			static_assert(sizeof(SortKey) == sizeof(uint64_t));
			SortKey a = {};
			a.bits.distance = distance;
			a.bits.sort_bits = sort_bits;
			a.bits.meshIndex = geometryIndex;
			SortKey b = {};
			b.bits.distance = other.distance;
			b.bits.sort_bits = other.sort_bits;
			b.bits.meshIndex = other.geometryIndex;
			return a.value > b.value;
		}
	};
	static_assert(sizeof(RenderBatch) == 16ull);

	struct RenderQueue
	{
		std::vector<RenderBatch> batches;

		inline void init()
		{
			batches.clear();
		}
		inline void add(uint32_t meshIndex, uint32_t instanceIndex, float distance, uint32_t sort_bits, uint16_t camera_mask = 0xFFFF)
		{
			batches.emplace_back().Create(meshIndex, instanceIndex, distance, sort_bits, camera_mask);
		}
		inline void sort_transparent()
		{
			std::sort(batches.begin(), batches.end(), std::greater<RenderBatch>());
		}
		inline void sort_opaque()
		{
			std::sort(batches.begin(), batches.end(), std::less<RenderBatch>());
		}
		inline bool empty() const
		{
			return batches.empty();
		}
		inline size_t size() const
		{
			return batches.size();
		}
	};
}

namespace vz
{
	using namespace renderer;

	static thread_local std::vector<GPUBarrier> barrierStack;
	static constexpr float foregroundDepthRange = 0.01f;

	using GPrimBuffers = GGeometryComponent::GPrimBuffers;
	using Primitive = GeometryComponent::Primitive;
	using GPrimEffectBuffers = GRenderableComponent::GPrimEffectBuffers;

	struct GRenderPath3DDetails : GRenderPath3D
	{
		GRenderPath3DDetails(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
			: GRenderPath3D(vp, swapChain, rtRenderFinal)
		{
			device = GetDevice();
		}

		GraphicsDevice* device = nullptr;
		bool viewShadingInCS = false;
		mutable bool firstFrame = true;

		FrameCB frameCB = {};
		// separate graphics pipelines for the combination of special rendering effects
		renderer::View viewMain;
		renderer::View viewReflection;

		// auxiliary cameras for special rendering effects
		CameraComponent cameraReflection = CameraComponent(0);
		CameraComponent cameraPrevious = CameraComponent(0);

		// resources associated with render target buffers and textures
		TemporalAAResources temporalAAResources; // dynamic allocation
		TiledLightResources tiledLightResources;
		//TiledLightResources tiledLightResources_planarReflection; // dynamic allocation
		LuminanceResources luminanceResources; // dynamic allocation
		BloomResources bloomResources;

		graphics::Texture rtShadingRate; // UINT8 shading rate per tile

		// aliased (rtPostprocess, rtPrimitiveID)

		graphics::Texture debugUAV; // debug UAV can be used by some shaders...
		graphics::Texture rtPostprocess; // ping-pong with main scene RT in post-process chain


		// temporal rt textures ... we need to reduce these textures (reuse others!!)
		//graphics::Texture rtDvrDepth; // aliased to rtPrimitiveID_2
		//graphics::Texture rtCounter; // aliased to rtPrimitiveID_1 ??


		graphics::Texture rtMain;
		graphics::Texture rtMain_render; // can be MSAA
		graphics::Texture rtPrimitiveID_1;	// aliasing to rtPostprocess
		graphics::Texture rtPrimitiveID_2;	// aliasing to rtDvrProcess
		graphics::Texture rtPrimitiveID_1_render; // can be MSAA
		graphics::Texture rtPrimitiveID_2_render; // can be MSAA
		graphics::Texture rtPrimitiveID_debug; // test

		graphics::Texture depthBufferMain; // used for depth-testing, can be MSAA
		graphics::Texture rtLinearDepth; // linear depth result + mipchain (max filter)
		graphics::Texture depthBuffer_Copy; // used for shader resource, single sample
		graphics::Texture depthBuffer_Copy1; // used for disocclusion check

		graphics::Texture rtParticleDistortion_render = {};
		graphics::Texture rtParticleDistortion = {};

		graphics::Texture distortion_overlay; // optional full screen distortion from an asset


		mutable const graphics::Texture* lastPostprocessRT = &rtPostprocess;

		//graphics::Texture reprojectedDepth; // prev frame depth reprojected into current, and downsampled for meshlet occlusion culling

		ViewResources viewResources;	// dynamic allocation

		// ---------- GRenderPath3D's internal impl.: -----------------
		//  * functions with an input 'CommandList' are to be implemented here, otherwise, implement 'renderer::' namespace

		void BarrierStackFlush(CommandList cmd)
		{
			if (barrierStack.empty())
				return;
			device->Barrier(barrierStack.data(), (uint32_t)barrierStack.size(), cmd);
			barrierStack.clear();
		}
		// call renderer::UpdatePerFrameData to update :
		//	1. viewMain
		//	2. frameCB
		void UpdateProcess(const float dt);
		// Updates the GPU state according to the previously called UpdatePerFrameData()
		void UpdateRenderData(const renderer::View& view, const FrameCB& frameCB, CommandList cmd);
		void UpdateRenderDataAsync(const renderer::View& view, const FrameCB& frameCB, CommandList cmd);

		void RefreshLightmaps(const Scene& scene, CommandList cmd);
		void RefreshWetmaps(const View& vis, CommandList cmd);
		
		void TextureStreamingReadbackCopy(const Scene& scene, graphics::CommandList cmd);

		void GenerateMipChain(const Texture& texture, MIPGENFILTER filter, CommandList cmd, const MIPGEN_OPTIONS& options);

		// Compress a texture into Block Compressed format
		//	textureSrc	: source uncompressed texture
		//	textureBC	: destination compressed texture, must be a supported BC format (BC1/BC3/BC4/BC5/BC6H_UFLOAT)
		//	Currently this will handle simple Texture2D with mip levels, and additionally BC6H cubemap
		void BlockCompress(const graphics::Texture& textureSrc, graphics::Texture& textureBC, graphics::CommandList cmd, uint32_t dstSliceOffset = 0);
		// Updates the per camera constant buffer need to call for each different camera that is used when calling DrawScene() and the like
		//	cameraPrevious : camera from previous frame, used for reprojection effects.
		//	cameraReflection : camera that renders planar reflection
		void BindCameraCB(const CameraComponent& camera, const CameraComponent& cameraPrevious, const CameraComponent& cameraReflection, CommandList cmd);
		void BindCommonResources(CommandList cmd)
		{
			device->BindConstantBuffer(&rcommon::buffers[BUFFERTYPE_FRAMECB], CBSLOT_RENDERER_FRAME, cmd);
		}

		void OcclusionCulling_Reset(const View& view, CommandList cmd);
		void OcclusionCulling_Render(const CameraComponent& camera, const View& view, CommandList cmd);
		void OcclusionCulling_Resolve(const View& view, CommandList cmd);
		
		void CreateTiledLightResources(TiledLightResources& res, XMUINT2 resolution);
		void ComputeTiledLightCulling(const TiledLightResources& res, const View& vis, const Texture& debugUAV, CommandList cmd);
		
		void CreateViewResources(ViewResources& res, XMUINT2 resolution);
		void View_Prepare(const ViewResources& res, const Texture& input_primitiveID_1, const Texture& input_primitiveID_2, CommandList cmd); // input_primitiveID can be MSAA
		// SURFACE need to be checked whether it requires FORWARD or DEFERRED
		void View_Surface(const ViewResources& res, const Texture& output, CommandList cmd); 
		void View_Surface_Reduced(const ViewResources& res, CommandList cmd);
		void View_Shade(const ViewResources& res, const Texture& output, CommandList cmd); 

		void DrawScene(const View& view, RENDERPASS renderPass, CommandList cmd, uint32_t flags);

		void RenderMeshes(const View& view, const RenderQueue& renderQueue, RENDERPASS renderPass, uint32_t filterMask, CommandList cmd, uint32_t flags = 0, uint32_t camera_count = 1);
		void RenderDirectVolumes(CommandList cmd);
		void RenderPostprocessChain(CommandList cmd);
		
		bool RenderProcess();
		void Compose(CommandList cmd);

		// ---------- Post Processings ----------
		void ProcessDeferredTextureRequests(CommandList cmd);
		void Postprocess_Blur_Gaussian(
			const Texture& input,
			const Texture& temp,
			const Texture& output,
			CommandList cmd,
			int mip_src,
			int mip_dst,
			bool wide
		);

		void Postprocess_Tonemap(
			const Texture& input,
			const Texture& output,
			CommandList cmd,
			float exposure,
			float brightness,
			float contrast,
			float saturation,
			bool dither,
			const Texture* texture_colorgradinglut,
			const Texture* texture_distortion,
			const GPUBuffer* buffer_luminance,
			const Texture* texture_bloom,
			ColorSpace display_colorspace,
			Tonemap tonemap,
			const Texture* texture_distortion_overlay,
			float hdr_calibration
		);

		// ---------- GRenderPath3D's interfaces: -----------------
		bool ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight) override; // must delete all canvas-related resources and re-create
		bool Render(const float dt) override;
		bool Destroy() override;
	};
}

namespace vz
{
	// camera-level GPU renderer updates
	//	c.f., scene-level (including animations) GPU-side updates performed in GSceneDetails::Update(..)
	void GRenderPath3DDetails::UpdateProcess(const float dt)
	{
		// Frustum culling for main camera:
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
			cameraReflection = *camera;
			cameraReflection.jitter = XMFLOAT2(0, 0);
			//cameraReflection.Reflect(viewMain.reflectionPlane);
			//viewReflection.layerMask = getLayerMask();
			viewReflection.scene = scene;
			viewReflection.camera = &cameraReflection;
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


		if (renderer::isTemporalAAEnabled)
		{
			const XMFLOAT4& halton = math::GetHaltonSequence(graphics::GetDevice()->GetFrameCount() % 256);
			camera->jitter.x = (halton.x * 2 - 1) / (float)internalResolution.x;
			camera->jitter.y = (halton.y * 2 - 1) / (float)internalResolution.y;

			cameraReflection.jitter.x = camera->jitter.x * 2;
			cameraReflection.jitter.y = camera->jitter.x * 2;

			if (!temporalAAResources.IsValid())
			{
				temporalAAResources.frame = 0;

				TextureDesc desc;
				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.format = Format::R11G11B10_FLOAT;
				desc.width = internalResolution.x;
				desc.height = internalResolution.y;
				device->CreateTexture(&desc, nullptr, &temporalAAResources.textureTemporal[0]);
				device->SetName(&temporalAAResources.textureTemporal[0], "TemporalAAResources::texture_temporal[0]");
				device->CreateTexture(&desc, nullptr, &temporalAAResources.textureTemporal[1]);
				device->SetName(&temporalAAResources.textureTemporal[1], "TemporalAAResources::texture_temporal[1]");
			}
		}
		else
		{
			camera->jitter = XMFLOAT2(0, 0);
			cameraReflection.jitter = XMFLOAT2(0, 0);
			temporalAAResources = {};
		}

		//TODO 
		if (false && !rtParticleDistortion.IsValid()) // rtAO or scene has waterRipples
		{
			TextureDesc desc;
			desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
			desc.format = Format::R16G16_FLOAT;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			desc.sample_count = 1;
			desc.misc_flags = ResourceMiscFlag::ALIASING_TEXTURE_RT_DS;
			device->CreateTexture(&desc, nullptr, &rtParticleDistortion);
			device->SetName(&rtParticleDistortion, "rtParticleDistortion");
			if (msaaSampleCount > 1)
			{
				desc.sample_count = msaaSampleCount;
				desc.misc_flags = ResourceMiscFlag::NONE;
				device->CreateTexture(&desc, nullptr, &rtParticleDistortion_render);
				device->SetName(&rtParticleDistortion_render, "rtParticleDistortion_render");
			}
			else
			{
				rtParticleDistortion_render = rtParticleDistortion;
			}
		}
		else
		{
			rtParticleDistortion = {};
			rtParticleDistortion_render = {};
		}

		if (false && !luminanceResources.luminance.IsValid())
		{
			float values[LUMINANCE_NUM_HISTOGRAM_BINS + 1 + 1] = {}; // 1 exposure + 1 luminance value + histogram
			GPUBufferDesc desc;
			desc.size = sizeof(values);
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.misc_flags = ResourceMiscFlag::BUFFER_RAW;
			device->CreateBuffer(&desc, values, &luminanceResources.luminance);
			device->SetName(&luminanceResources.luminance, "luminance");
		}
		else
		{
			luminanceResources = {};
		}

		if (false && !bloomResources.texture_bloom.IsValid())
		{
			TextureDesc desc;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.format = Format::R11G11B10_FLOAT;
			desc.width = internalResolution.x / 4;
			desc.height = internalResolution.y / 4;
			desc.mip_levels = std::min(5u, (uint32_t)std::log2(std::max(desc.width, desc.height)));
			device->CreateTexture(&desc, nullptr, &bloomResources.texture_bloom);
			device->SetName(&bloomResources.texture_bloom, "bloom.texture_bloom");
			device->CreateTexture(&desc, nullptr, &bloomResources.texture_temp);
			device->SetName(&bloomResources.texture_temp, "bloom.texture_temp");

			for (uint32_t i = 0; i < bloomResources.texture_bloom.desc.mip_levels; ++i)
			{
				int subresource_index;
				subresource_index = device->CreateSubresource(&bloomResources.texture_bloom, SubresourceType::SRV, 0, 1, i, 1);
				assert(subresource_index == i);
				subresource_index = device->CreateSubresource(&bloomResources.texture_temp, SubresourceType::SRV, 0, 1, i, 1);
				assert(subresource_index == i);
				subresource_index = device->CreateSubresource(&bloomResources.texture_bloom, SubresourceType::UAV, 0, 1, i, 1);
				assert(subresource_index == i);
				subresource_index = device->CreateSubresource(&bloomResources.texture_temp, SubresourceType::UAV, 0, 1, i, 1);
				assert(subresource_index == i);
			}
		}
		else
		{
			bloomResources = {};
		}

		// the given CameraComponent
		viewMain.camera->UpdateMatrix();

		if (viewMain.isPlanarReflectionVisible)
		{
			cameraReflection.UpdateMatrix();
		}

		// Check whether shadow mask is required:
		//if (renderer::GetScreenSpaceShadowsEnabled() || renderer::GetRaytracedShadowsEnabled())
		//{
		//	if (!rtShadow.IsValid())
		//	{
		//		TextureDesc desc;
		//		desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
		//		desc.format = Format::R8_UNORM;
		//		desc.array_size = 16;
		//		desc.width = canvasWidth;
		//		desc.height = canvasHeight;
		//		desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
		//		device->CreateTexture(&desc, nullptr, &rtShadow);
		//		device->SetName(&rtShadow, "rtShadow");
		//	}
		//}
		//else
		//{
		//	rtShadow = {};
		//}

		if (renderer::isFSREnabled)
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
			viewShadingInCS// ||
			//getSSREnabled() ||
			//getSSGIEnabled() ||
			//getRaytracedReflectionEnabled() ||
			//getRaytracedDiffuseEnabled() ||
			//GetScreenSpaceShadowsEnabled() ||
			//GetRaytracedShadowsEnabled() ||
			//GetVXGIEnabled()
			)
		{
			if (!viewResources.IsValid())
			{
				CreateViewResources(viewResources, internalResolution);
			}
		}
		else
		{
			viewResources = {};
		}

		// Keep a copy of last frame's depth buffer for temporal disocclusion checks, so swap with current one every frame:
		std::swap(depthBuffer_Copy, depthBuffer_Copy1);

		viewResources.depthbuffer = &depthBuffer_Copy;
		viewResources.lineardepth = &rtLinearDepth;
		if (msaaSampleCount > 1)
		{
			viewResources.primitiveID_1_resolved = &rtPrimitiveID_1;
			viewResources.primitiveID_2_resolved = &rtPrimitiveID_2;
		}
		else
		{
			viewResources.primitiveID_1_resolved = nullptr;
			viewResources.primitiveID_2_resolved = nullptr;
		}
	}

	void GRenderPath3DDetails::ProcessDeferredTextureRequests(CommandList cmd)
	{
		if (rcommon::deferredMIPGens.size() + rcommon::deferredBCQueue.size()
			+ rcommon::deferredBufferUpdate.size() + rcommon::deferredTextureCopy.size() == 0)
		{
			return;
		}

		// TODO: paint texture...
		rcommon::deferredResourceLock.lock();

		for (auto& it : rcommon::deferredMIPGens)
		{
			MIPGEN_OPTIONS mipopt;
			mipopt.preserve_coverage = it.second;
			GenerateMipChain(it.first, MIPGENFILTER_LINEAR, cmd, mipopt);
		}

		rcommon::deferredMIPGens.clear();
		for (auto& it : rcommon::deferredBCQueue)
		{
			BlockCompress(it.first, it.second, cmd);
		}
		rcommon::deferredBCQueue.clear();

		for (auto& it : rcommon::deferredBufferUpdate)
		{
			GPUBuffer& buffer = it.first;
			void* data = it.second.first;
			size_t size = it.second.second;
			
			device->Barrier(GPUBarrier::Buffer(&buffer, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::COPY_DST), cmd);
			device->UpdateBuffer(&buffer, data, cmd);
			device->Barrier(GPUBarrier::Buffer(&buffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE_COMPUTE), cmd);
		}
		rcommon::deferredBufferUpdate.clear();

		for (auto& it : rcommon::deferredTextureCopy)
		{
			Texture& src = it.first;
			Texture& dst = it.second;
			GPUBarrier barriers1[] = {
				GPUBarrier::Image(&src, src.desc.layout, ResourceState::COPY_SRC),
				GPUBarrier::Image(&dst, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::COPY_DST),
			};
			device->Barrier(barriers1, arraysize(barriers1), cmd);
			device->CopyResource(&dst, &src, cmd);
			GPUBarrier barriers2[] = {
				GPUBarrier::Image(&src, ResourceState::COPY_SRC, src.desc.layout),
				GPUBarrier::Image(&dst, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE_COMPUTE),
			};
			device->Barrier(barriers2, arraysize(barriers2), cmd);
		}
		rcommon::deferredTextureCopy.clear();

		rcommon::deferredResourceLock.unlock();
	}

	void GRenderPath3DDetails::Postprocess_Blur_Gaussian(
		const Texture& input,
		const Texture& temp,
		const Texture& output,
		CommandList cmd,
		int mip_src,
		int mip_dst,
		bool wide
	)
	{
		device->EventBegin("Postprocess_Blur_Gaussian", cmd);

		SHADERTYPE cs = CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT4;
		switch (output.GetDesc().format)
		{
		case Format::R16_UNORM:
		case Format::R8_UNORM:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_UNORM1 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_UNORM1;
			break;
		case Format::R16_FLOAT:
		case Format::R32_FLOAT:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT1 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT1;
			break;
		case Format::R16G16B16A16_UNORM:
		case Format::R8G8B8A8_UNORM:
		case Format::B8G8R8A8_UNORM:
		case Format::R10G10B10A2_UNORM:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_UNORM4 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_UNORM4;
			break;
		case Format::R11G11B10_FLOAT:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT3 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT3;
			break;
		case Format::R16G16B16A16_FLOAT:
		case Format::R32G32B32A32_FLOAT:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT4 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT4;
			break;
		default:
			assert(0); // implement format!
			break;
		}
		device->BindComputeShader(&rcommon::shaders[cs], cmd);

		// Horizontal:
		{
			const TextureDesc& desc = temp.GetDesc();

			PostProcess postprocess;
			postprocess.resolution.x = desc.width;
			postprocess.resolution.y = desc.height;
			if (mip_dst > 0)
			{
				postprocess.resolution.x >>= mip_dst;
				postprocess.resolution.y >>= mip_dst;
			}
			postprocess.resolution_rcp.x = 1.0f / postprocess.resolution.x;
			postprocess.resolution_rcp.y = 1.0f / postprocess.resolution.y;
			postprocess.params0.x = 1;
			postprocess.params0.y = 0;
			device->PushConstants(&postprocess, sizeof(postprocess), cmd);

			device->BindResource(&input, 0, cmd, mip_src);
			device->BindUAV(&temp, 0, cmd, mip_dst);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&temp, temp.desc.layout, ResourceState::UNORDERED_ACCESS, mip_dst),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->Dispatch(
				(postprocess.resolution.x + POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT - 1) / POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT,
				postprocess.resolution.y,
				1,
				cmd
			);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&temp, ResourceState::UNORDERED_ACCESS, temp.desc.layout, mip_dst),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

		}

		// Vertical:
		{
			const TextureDesc& desc = output.GetDesc();

			PostProcess postprocess;
			postprocess.resolution.x = desc.width;
			postprocess.resolution.y = desc.height;
			if (mip_dst > 0)
			{
				postprocess.resolution.x >>= mip_dst;
				postprocess.resolution.y >>= mip_dst;
			}
			postprocess.resolution_rcp.x = 1.0f / postprocess.resolution.x;
			postprocess.resolution_rcp.y = 1.0f / postprocess.resolution.y;
			postprocess.params0.x = 0;
			postprocess.params0.y = 1;
			device->PushConstants(&postprocess, sizeof(postprocess), cmd);

			device->BindResource(&temp, 0, cmd, mip_dst); // <- also mip_dst because it's second pass!
			device->BindUAV(&output, 0, cmd, mip_dst);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&output, output.desc.layout, ResourceState::UNORDERED_ACCESS, mip_dst),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->Dispatch(
				postprocess.resolution.x,
				(postprocess.resolution.y + POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT - 1) / POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT,
				1,
				cmd
			);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&output, ResourceState::UNORDERED_ACCESS, output.desc.layout, mip_dst),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

		}

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::GenerateMipChain(const Texture& texture, MIPGENFILTER filter, CommandList cmd, const MIPGEN_OPTIONS& options)
	{
		if (!texture.IsValid())
		{
			assert(0);
			return;
		}

		TextureDesc desc = texture.GetDesc();

		if (desc.mip_levels < 2)
		{
			assert(0);
			return;
		}

		bool hdr = !IsFormatUnorm(desc.format);

		MipgenPushConstants mipgen = {};

		if (options.preserve_coverage)
		{
			mipgen.mipgen_options |= MIPGEN_OPTION_BIT_PRESERVE_COVERAGE;
		}
		if (IsFormatSRGB(desc.format))
		{
			mipgen.mipgen_options |= MIPGEN_OPTION_BIT_SRGB;
		}

		if (desc.type == TextureDesc::Type::TEXTURE_1D)
		{
			assert(0); // not implemented
		}
		else if (desc.type == TextureDesc::Type::TEXTURE_2D)
		{

			if (has_flag(desc.misc_flags, ResourceMiscFlag::TEXTURECUBE))
			{

				if (desc.array_size > 6)
				{
					// Cubearray
					assert(options.arrayIndex >= 0 && "You should only filter a specific cube in the array for now, so provide its index!");

					switch (filter)
					{
					case MIPGENFILTER_POINT:
						device->EventBegin("GenerateMipChain CubeArray - PointFilter", cmd);
						device->BindComputeShader(&rcommon::shaders[hdr ? CSTYPE_GENERATEMIPCHAINCUBEARRAY_FLOAT4 : CSTYPE_GENERATEMIPCHAINCUBEARRAY_UNORM4], cmd);
						mipgen.sampler_index = device->GetDescriptorIndex(&rcommon::samplers[SAMPLER_POINT_CLAMP]);
						break;
					case MIPGENFILTER_LINEAR:
						device->EventBegin("GenerateMipChain CubeArray - LinearFilter", cmd);
						device->BindComputeShader(&rcommon::shaders[hdr ? CSTYPE_GENERATEMIPCHAINCUBEARRAY_FLOAT4 : CSTYPE_GENERATEMIPCHAINCUBEARRAY_UNORM4], cmd);
						mipgen.sampler_index = device->GetDescriptorIndex(&rcommon::samplers[SAMPLER_LINEAR_CLAMP]);
						break;
					default:
						assert(0);
						break;
					}

					for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
					{
						{
							GPUBarrier barriers[] = {
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 0),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 1),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 2),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 3),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 4),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, options.arrayIndex * 6 + 5),
							};
							device->Barrier(barriers, arraysize(barriers), cmd);
						}

						mipgen.texture_output = device->GetDescriptorIndex(&texture, SubresourceType::UAV, i + 1);
						mipgen.texture_input = device->GetDescriptorIndex(&texture, SubresourceType::SRV, i);
						desc.width = std::max(1u, desc.width / 2);
						desc.height = std::max(1u, desc.height / 2);

						mipgen.outputResolution.x = desc.width;
						mipgen.outputResolution.y = desc.height;
						mipgen.outputResolution_rcp.x = 1.0f / mipgen.outputResolution.x;
						mipgen.outputResolution_rcp.y = 1.0f / mipgen.outputResolution.y;
						mipgen.arrayIndex = options.arrayIndex;
						device->PushConstants(&mipgen, sizeof(mipgen), cmd);

						device->Dispatch(
							std::max(1u, (desc.width + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
							std::max(1u, (desc.height + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
							6,
							cmd);

						{
							GPUBarrier barriers[] = {
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 0),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 1),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 2),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 3),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 4),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, options.arrayIndex * 6 + 5),
							};
							device->Barrier(barriers, arraysize(barriers), cmd);
						}
					}
				}
				else
				{
					// Cubemap
					switch (filter)
					{
					case MIPGENFILTER_POINT:
						device->EventBegin("GenerateMipChain Cube - PointFilter", cmd);
						device->BindComputeShader(&rcommon::shaders[hdr ? CSTYPE_GENERATEMIPCHAINCUBE_FLOAT4 : CSTYPE_GENERATEMIPCHAINCUBE_UNORM4], cmd);
						mipgen.sampler_index = device->GetDescriptorIndex(&rcommon::samplers[SAMPLER_POINT_CLAMP]);
						break;
					case MIPGENFILTER_LINEAR:
						device->EventBegin("GenerateMipChain Cube - LinearFilter", cmd);
						device->BindComputeShader(&rcommon::shaders[hdr ? CSTYPE_GENERATEMIPCHAINCUBE_FLOAT4 : CSTYPE_GENERATEMIPCHAINCUBE_UNORM4], cmd);
						mipgen.sampler_index = device->GetDescriptorIndex(&rcommon::samplers[SAMPLER_LINEAR_CLAMP]);
						break;
					default:
						assert(0); // not implemented
						break;
					}

					for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
					{
						{
							GPUBarrier barriers[] = {
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 0),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 1),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 2),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 3),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 4),
								GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS, i + 1, 5),
							};
							device->Barrier(barriers, arraysize(barriers), cmd);
						}

						mipgen.texture_output = device->GetDescriptorIndex(&texture, SubresourceType::UAV, i + 1);
						mipgen.texture_input = device->GetDescriptorIndex(&texture, SubresourceType::SRV, i);
						desc.width = std::max(1u, desc.width / 2);
						desc.height = std::max(1u, desc.height / 2);

						mipgen.outputResolution.x = desc.width;
						mipgen.outputResolution.y = desc.height;
						mipgen.outputResolution_rcp.x = 1.0f / mipgen.outputResolution.x;
						mipgen.outputResolution_rcp.y = 1.0f / mipgen.outputResolution.y;
						mipgen.arrayIndex = 0;
						device->PushConstants(&mipgen, sizeof(mipgen), cmd);

						device->Dispatch(
							std::max(1u, (desc.width + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
							std::max(1u, (desc.height + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
							6,
							cmd);

						{
							GPUBarrier barriers[] = {
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 0),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 1),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 2),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 3),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 4),
								GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout, i + 1, 5),
							};
							device->Barrier(barriers, arraysize(barriers), cmd);
						}
					}
				}

			}
			else
			{
				// Texture
				switch (filter)
				{
				case MIPGENFILTER_POINT:
					device->EventBegin("GenerateMipChain 2D - PointFilter", cmd);
					device->BindComputeShader(&rcommon::shaders[hdr ? CSTYPE_GENERATEMIPCHAIN2D_FLOAT4 : CSTYPE_GENERATEMIPCHAIN2D_UNORM4], cmd);
					mipgen.sampler_index = device->GetDescriptorIndex(&rcommon::samplers[SAMPLER_POINT_CLAMP]);
					break;
				case MIPGENFILTER_LINEAR:
					device->EventBegin("GenerateMipChain 2D - LinearFilter", cmd);
					device->BindComputeShader(&rcommon::shaders[hdr ? CSTYPE_GENERATEMIPCHAIN2D_FLOAT4 : CSTYPE_GENERATEMIPCHAIN2D_UNORM4], cmd);
					mipgen.sampler_index = device->GetDescriptorIndex(&rcommon::samplers[SAMPLER_LINEAR_CLAMP]);
					break;
				case MIPGENFILTER_GAUSSIAN:
				{
					assert(options.gaussian_temp != nullptr); // needed for separate filter!
					device->EventBegin("GenerateMipChain 2D - GaussianFilter", cmd);
					// Gaussian filter is a bit different as we do it in a separable way:
					for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
					{
						Postprocess_Blur_Gaussian(texture, *options.gaussian_temp, texture, cmd, i, i + 1, options.wide_gauss);
					}
					device->EventEnd(cmd);
					return;
				}
				break;
				default:
					assert(0);
					break;
				}

				for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
				{
					{
						GPUBarrier barriers[] = {
							GPUBarrier::Image(&texture,texture.desc.layout,ResourceState::UNORDERED_ACCESS,i + 1),
						};
						device->Barrier(barriers, arraysize(barriers), cmd);
					}

					mipgen.texture_output = device->GetDescriptorIndex(&texture, SubresourceType::UAV, i + 1);
					mipgen.texture_input = device->GetDescriptorIndex(&texture, SubresourceType::SRV, i);
					desc.width = std::max(1u, desc.width / 2);
					desc.height = std::max(1u, desc.height / 2);

					mipgen.outputResolution.x = desc.width;
					mipgen.outputResolution.y = desc.height;
					mipgen.outputResolution_rcp.x = 1.0f / mipgen.outputResolution.x;
					mipgen.outputResolution_rcp.y = 1.0f / mipgen.outputResolution.y;
					mipgen.arrayIndex = options.arrayIndex >= 0 ? (uint)options.arrayIndex : 0;
					device->PushConstants(&mipgen, sizeof(mipgen), cmd);

					device->Dispatch(
						std::max(1u, (desc.width + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
						std::max(1u, (desc.height + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE),
						1,
						cmd);

					{
						GPUBarrier barriers[] = {
							GPUBarrier::Image(&texture,ResourceState::UNORDERED_ACCESS,texture.desc.layout,i + 1),
						};
						device->Barrier(barriers, arraysize(barriers), cmd);
					}
				}
			}


			device->EventEnd(cmd);
		}
		else if (desc.type == TextureDesc::Type::TEXTURE_3D)
		{
			switch (filter)
			{
			case MIPGENFILTER_POINT:
				device->EventBegin("GenerateMipChain 3D - PointFilter", cmd);
				device->BindComputeShader(&rcommon::shaders[hdr ? CSTYPE_GENERATEMIPCHAIN3D_FLOAT4 : CSTYPE_GENERATEMIPCHAIN3D_UNORM4], cmd);
				mipgen.sampler_index = device->GetDescriptorIndex(&rcommon::samplers[SAMPLER_POINT_CLAMP]);
				break;
			case MIPGENFILTER_LINEAR:
				device->EventBegin("GenerateMipChain 3D - LinearFilter", cmd);
				device->BindComputeShader(&rcommon::shaders[hdr ? CSTYPE_GENERATEMIPCHAIN3D_FLOAT4 : CSTYPE_GENERATEMIPCHAIN3D_UNORM4], cmd);
				mipgen.sampler_index = device->GetDescriptorIndex(&rcommon::samplers[SAMPLER_LINEAR_CLAMP]);
				break;
			default:
				assert(0); // not implemented
				break;
			}

			for (uint32_t i = 0; i < desc.mip_levels - 1; ++i)
			{
				mipgen.texture_output = device->GetDescriptorIndex(&texture, SubresourceType::UAV, i + 1);
				mipgen.texture_input = device->GetDescriptorIndex(&texture, SubresourceType::SRV, i);
				desc.width = std::max(1u, desc.width / 2);
				desc.height = std::max(1u, desc.height / 2);
				desc.depth = std::max(1u, desc.depth / 2);

				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&texture,texture.desc.layout,ResourceState::UNORDERED_ACCESS,i + 1),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}

				mipgen.outputResolution.x = desc.width;
				mipgen.outputResolution.y = desc.height;
				mipgen.outputResolution.z = desc.depth;
				mipgen.outputResolution_rcp.x = 1.0f / mipgen.outputResolution.x;
				mipgen.outputResolution_rcp.y = 1.0f / mipgen.outputResolution.y;
				mipgen.outputResolution_rcp.z = 1.0f / mipgen.outputResolution.z;
				mipgen.arrayIndex = options.arrayIndex >= 0 ? (uint)options.arrayIndex : 0;
				mipgen.mipgen_options = 0;
				device->PushConstants(&mipgen, sizeof(mipgen), cmd);

				device->Dispatch(
					std::max(1u, (desc.width + GENERATEMIPCHAIN_3D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_3D_BLOCK_SIZE),
					std::max(1u, (desc.height + GENERATEMIPCHAIN_3D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_3D_BLOCK_SIZE),
					std::max(1u, (desc.depth + GENERATEMIPCHAIN_3D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_3D_BLOCK_SIZE),
					cmd);

				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&texture,ResourceState::UNORDERED_ACCESS,texture.desc.layout,i + 1),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}
			}


			device->EventEnd(cmd);
		}
		else
		{
			assert(0);
		}
	}

	void GRenderPath3DDetails::BlockCompress(const graphics::Texture& texture_src, graphics::Texture& texture_bc, graphics::CommandList cmd, uint32_t dst_slice_offset)

	{
		const uint32_t block_size = GetFormatBlockSize(texture_bc.desc.format);
		TextureDesc desc;
		desc.width = std::max(1u, texture_bc.desc.width / block_size);
		desc.height = std::max(1u, texture_bc.desc.height / block_size);
		desc.bind_flags = BindFlag::UNORDERED_ACCESS;
		desc.layout = ResourceState::UNORDERED_ACCESS;

		Texture bc_raw_dest;
		{
			// Find a raw block texture that will fit the request:
			static std::mutex locker;
			std::scoped_lock lock(locker);
			static Texture bc_raw_uint2;
			static Texture bc_raw_uint4;
			static Texture bc_raw_uint4_cubemap;
			Texture* bc_raw = nullptr;
			switch (texture_bc.desc.format)
			{
			case Format::BC1_UNORM:
			case Format::BC1_UNORM_SRGB:
				desc.format = Format::R32G32_UINT;
				bc_raw = &bc_raw_uint2;
				device->BindComputeShader(&rcommon::shaders[CSTYPE_BLOCKCOMPRESS_BC1], cmd);
				device->EventBegin("BlockCompress - BC1", cmd);
				break;
			case Format::BC3_UNORM:
			case Format::BC3_UNORM_SRGB:
				desc.format = Format::R32G32B32A32_UINT;
				bc_raw = &bc_raw_uint4;
				device->BindComputeShader(&rcommon::shaders[CSTYPE_BLOCKCOMPRESS_BC3], cmd);
				device->EventBegin("BlockCompress - BC3", cmd);
				break;
			case Format::BC4_UNORM:
				desc.format = Format::R32G32_UINT;
				bc_raw = &bc_raw_uint2;
				device->BindComputeShader(&rcommon::shaders[CSTYPE_BLOCKCOMPRESS_BC4], cmd);
				device->EventBegin("BlockCompress - BC4", cmd);
				break;
			case Format::BC5_UNORM:
				desc.format = Format::R32G32B32A32_UINT;
				bc_raw = &bc_raw_uint4;
				device->BindComputeShader(&rcommon::shaders[CSTYPE_BLOCKCOMPRESS_BC5], cmd);
				device->EventBegin("BlockCompress - BC5", cmd);
				break;
			case Format::BC6H_UF16:
				desc.format = Format::R32G32B32A32_UINT;
				if (has_flag(texture_src.desc.misc_flags, ResourceMiscFlag::TEXTURECUBE))
				{
					bc_raw = &bc_raw_uint4_cubemap;
					device->BindComputeShader(&rcommon::shaders[CSTYPE_BLOCKCOMPRESS_BC6H_CUBEMAP], cmd);
					device->EventBegin("BlockCompress - BC6H - Cubemap", cmd);
					desc.array_size = texture_src.desc.array_size; // src array size not dst!!
				}
				else
				{
					bc_raw = &bc_raw_uint4;
					device->BindComputeShader(&rcommon::shaders[CSTYPE_BLOCKCOMPRESS_BC6H], cmd);
					device->EventBegin("BlockCompress - BC6H", cmd);
				}
				break;
			default:
				assert(0); // not supported
				return;
			}

			if (!bc_raw->IsValid() || bc_raw->desc.width < desc.width || bc_raw->desc.height < desc.height || bc_raw->desc.array_size < desc.array_size)
			{
				TextureDesc bc_raw_desc = desc;
				bc_raw_desc.width = std::max(64u, bc_raw_desc.width);
				bc_raw_desc.height = std::max(64u, bc_raw_desc.height);
				bc_raw_desc.width = std::max(bc_raw->desc.width, bc_raw_desc.width);
				bc_raw_desc.height = std::max(bc_raw->desc.height, bc_raw_desc.height);
				bc_raw_desc.width = math::GetNextPowerOfTwo(bc_raw_desc.width);
				bc_raw_desc.height = math::GetNextPowerOfTwo(bc_raw_desc.height);
				device->CreateTexture(&bc_raw_desc, nullptr, bc_raw);
				device->SetName(bc_raw, "bc_raw");

				device->ClearUAV(bc_raw, 0, cmd);
				device->Barrier(GPUBarrier::Memory(bc_raw), cmd);

				std::string info;
				info += "BlockCompress created a new raw block texture to fit request: " + std::string(GetFormatString(texture_bc.desc.format)) + " (" + std::to_string(texture_bc.desc.width) + ", " + std::to_string(texture_bc.desc.height) + ")";
				info += "\n\tFormat = ";
				info += GetFormatString(bc_raw_desc.format);
				info += "\n\tResolution = " + std::to_string(bc_raw_desc.width) + " * " + std::to_string(bc_raw_desc.height);
				info += "\n\tArray Size = " + std::to_string(bc_raw_desc.array_size);
				size_t total_size = 0;
				total_size += ComputeTextureMemorySizeInBytes(bc_raw_desc);
				info += "\n\tMemory = " + helper::GetMemorySizeText(total_size) + "\n";
				backlog::post(info);
			}

			bc_raw_dest = *bc_raw;
		}

		for (uint32_t mip = 0; mip < texture_bc.desc.mip_levels; ++mip)
		{
			const uint32_t width = std::max(1u, desc.width >> mip);
			const uint32_t height = std::max(1u, desc.height >> mip);
			device->BindResource(&texture_src, 0, cmd, texture_src.desc.mip_levels == 1 ? -1 : mip);
			device->BindUAV(&bc_raw_dest, 0, cmd);
			device->Dispatch((width + 7u) / 8u, (height + 7u) / 8u, desc.array_size, cmd);

			GPUBarrier barriers[] = {
				GPUBarrier::Image(&bc_raw_dest, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SRC),
				GPUBarrier::Image(&texture_bc, texture_bc.desc.layout, ResourceState::COPY_DST),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);

			for (uint32_t slice = 0; slice < desc.array_size; ++slice)
			{
				Box box;
				box.left = 0;
				box.right = width;
				box.top = 0;
				box.bottom = height;
				box.front = 0;
				box.back = 1;

				device->CopyTexture(
					&texture_bc, 0, 0, 0, mip, dst_slice_offset + slice,
					&bc_raw_dest, 0, slice,
					cmd,
					&box
				);
			}

			for (int i = 0; i < arraysize(barriers); ++i)
			{
				std::swap(barriers[i].image.layout_before, barriers[i].image.layout_after);
			}
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->EventEnd(cmd);
	}

	// note : this function is supposed to be called in jobsystem
	//	so, the internal parameters must not be declared outside the function (e.g., member parameter)
	void GRenderPath3DDetails::BindCameraCB(const CameraComponent& camera, const CameraComponent& cameraPrevious, const CameraComponent& cameraReflection, CommandList cmd)
	{
		CameraCB* cameraCB = new CameraCB();
		cameraCB->Init();
		ShaderCamera& shadercam = cameraCB->cameras[0];

		// NOTE:
		//  the following parameters need to be set according to 
		//	* shadercam.options : RenderPath3D's property
		//  * shadercam.clip_plane  : RenderPath3D's property
		//  * shadercam.reflection_plane : Scene's property

		shadercam.options = SHADERCAMERA_OPTION_NONE;//camera.shadercamera_options;
		//if (camera.IsOrtho())
		//{
		//	shadercam.options |= SHADERCAMERA_OPTION_ORTHO;
		//}

		shadercam.view_projection = camera.GetViewProjection();
		shadercam.view = camera.GetView();
		shadercam.projection = camera.GetProjection();
		shadercam.position = camera.GetWorldEye();
		shadercam.inverse_view = camera.GetInvView();
		shadercam.inverse_projection = camera.GetInvProjection();
		shadercam.inverse_view_projection = camera.GetInvViewProjection();
		XMMATRIX invVP = XMLoadFloat4x4(&shadercam.inverse_view_projection);
		shadercam.forward = camera.GetWorldForward();
		shadercam.up = camera.GetWorldUp();
		camera.GetNearFar(&shadercam.z_near, &shadercam.z_far);
		shadercam.z_near_rcp = 1.0f / std::max(0.0001f, shadercam.z_near);
		shadercam.z_far_rcp = 1.0f / std::max(0.0001f, shadercam.z_far);
		shadercam.z_range = abs(shadercam.z_far - shadercam.z_near);
		shadercam.z_range_rcp = 1.0f / std::max(0.0001f, shadercam.z_range);
		shadercam.clip_plane = XMFLOAT4(0, 0, 0, 0); // default: no clip plane
		shadercam.reflection_plane = XMFLOAT4(0, 0, 0, 0);

		const Frustum& cam_frustum = camera.GetFrustum();
		static_assert(arraysize(cam_frustum.planes) == arraysize(shadercam.frustum.planes), "Mismatch!");
		for (int i = 0; i < arraysize(cam_frustum.planes); ++i)
		{
			shadercam.frustum.planes[i] = cam_frustum.planes[i];
		}
		XMStoreFloat4(&shadercam.frustum_corners.cornersNEAR[0], XMVector3TransformCoord(XMVectorSet(-1, 1, 1, 1), invVP));
		XMStoreFloat4(&shadercam.frustum_corners.cornersNEAR[1], XMVector3TransformCoord(XMVectorSet(1, 1, 1, 1), invVP));
		XMStoreFloat4(&shadercam.frustum_corners.cornersNEAR[2], XMVector3TransformCoord(XMVectorSet(-1, -1, 1, 1), invVP));
		XMStoreFloat4(&shadercam.frustum_corners.cornersNEAR[3], XMVector3TransformCoord(XMVectorSet(1, -1, 1, 1), invVP));

		XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[0], XMVector3TransformCoord(XMVectorSet(-1, 1, 0, 1), invVP));
		XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[1], XMVector3TransformCoord(XMVectorSet(1, 1, 0, 1), invVP));
		XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[2], XMVector3TransformCoord(XMVectorSet(-1, -1, 0, 1), invVP));
		XMStoreFloat4(&shadercam.frustum_corners.cornersFAR[3], XMVector3TransformCoord(XMVectorSet(1, -1, 0, 1), invVP));

		shadercam.temporalaa_jitter = camera.jitter;
		shadercam.temporalaa_jitter_prev = cameraPrevious.jitter;

		shadercam.previous_view = cameraPrevious.GetView();
		shadercam.previous_projection = cameraPrevious.GetProjection();
		shadercam.previous_view_projection = cameraPrevious.GetViewProjection();
		shadercam.previous_inverse_view_projection = cameraPrevious.GetInvViewProjection();
		shadercam.reflection_view_projection = cameraReflection.GetViewProjection();
		shadercam.reflection_inverse_view_projection = cameraReflection.GetInvViewProjection();
		XMStoreFloat4x4(&shadercam.reprojection,
			XMLoadFloat4x4(&camera.GetInvViewProjection()) * XMLoadFloat4x4(&cameraPrevious.GetViewProjection()));

		shadercam.focal_length = camera.GetFocalLength();
		shadercam.aperture_size = camera.GetApertureSize();
		shadercam.aperture_shape = camera.GetApertureShape();

		shadercam.internal_resolution = uint2((uint)canvasWidth_, (uint)canvasHeight_);
		shadercam.internal_resolution_rcp = float2(1.0f / std::max(1u, shadercam.internal_resolution.x), 1.0f / std::max(1u, shadercam.internal_resolution.y));

		shadercam.scissor.x = scissor.left;
		shadercam.scissor.y = scissor.top;
		shadercam.scissor.z = scissor.right;
		shadercam.scissor.w = scissor.bottom;

		// scissor_uv is also offset by 0.5 (half pixel) to avoid going over last pixel center with bilinear sampler:
		shadercam.scissor_uv.x = (shadercam.scissor.x + 0.5f) * shadercam.internal_resolution_rcp.x;
		shadercam.scissor_uv.y = (shadercam.scissor.y + 0.5f) * shadercam.internal_resolution_rcp.y;
		shadercam.scissor_uv.z = (shadercam.scissor.z - 0.5f) * shadercam.internal_resolution_rcp.x;
		shadercam.scissor_uv.w = (shadercam.scissor.w - 0.5f) * shadercam.internal_resolution_rcp.y;

		shadercam.entity_culling_tilecount = GetEntityCullingTileCount(shadercam.internal_resolution);
		shadercam.entity_culling_tile_bucket_count_flat = shadercam.entity_culling_tilecount.x * shadercam.entity_culling_tilecount.y * SHADER_ENTITY_TILE_BUCKET_COUNT;
		shadercam.sample_count = depthBufferMain.desc.sample_count;
		shadercam.visibility_tilecount = GetViewTileCount(shadercam.internal_resolution);
		shadercam.visibility_tilecount_flat = shadercam.visibility_tilecount.x * shadercam.visibility_tilecount.y;

		shadercam.texture_primitiveID_1_index = device->GetDescriptorIndex(&rtPrimitiveID_1, SubresourceType::SRV);
		shadercam.texture_primitiveID_2_index = device->GetDescriptorIndex(&rtPrimitiveID_2, SubresourceType::SRV);
		shadercam.texture_depth_index = device->GetDescriptorIndex(&depthBuffer_Copy, SubresourceType::SRV);
		shadercam.texture_lineardepth_index = device->GetDescriptorIndex(&rtLinearDepth, SubresourceType::SRV);
		//shadercam.texture_velocity_index = camera.texture_velocity_index;
		shadercam.texture_normal_index = device->GetDescriptorIndex(&viewResources.texture_normals, SubresourceType::SRV);
		shadercam.texture_roughness_index = device->GetDescriptorIndex(&viewResources.texture_roughness, SubresourceType::SRV);
		shadercam.buffer_entitytiles_index = device->GetDescriptorIndex(&tiledLightResources.entityTiles, SubresourceType::SRV);
		//shadercam.texture_reflection_index = camera.texture_reflection_index;
		//shadercam.texture_reflection_depth_index = camera.texture_reflection_depth_index;
		//shadercam.texture_refraction_index = camera.texture_refraction_index;
		//shadercam.texture_waterriples_index = camera.texture_waterriples_index;
		//shadercam.texture_ao_index = camera.texture_ao_index;
		//shadercam.texture_ssr_index = camera.texture_ssr_index;
		//shadercam.texture_ssgi_index = camera.texture_ssgi_index;
		//shadercam.texture_rtshadow_index = camera.texture_rtshadow_index;
		//shadercam.texture_rtdiffuse_index = camera.texture_rtdiffuse_index;
		//shadercam.texture_surfelgi_index = camera.texture_surfelgi_index;
		//shadercam.texture_depth_index_prev = cameraPrevious.texture_depth_index;
		//shadercam.texture_vxgi_diffuse_index = camera.texture_vxgi_diffuse_index;
		//shadercam.texture_vxgi_specular_index = camera.texture_vxgi_specular_index;
		//shadercam.texture_reprojected_depth_index = camera.texture_reprojected_depth_index;
		
		device->BindDynamicConstantBuffer(*cameraCB, CBSLOT_RENDERER_CAMERA, cmd);
		delete cameraCB;
	}
	void GRenderPath3DDetails::UpdateRenderData(const View& view, const FrameCB& frameCB, CommandList cmd)
	{
		device->EventBegin("UpdateRenderData", cmd);

		auto prof_updatebuffer_cpu = profiler::BeginRangeCPU("Update Buffers (CPU)");
		auto prof_updatebuffer_gpu = profiler::BeginRangeGPU("Update Buffers (GPU)", &cmd);

		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		barrierStack.push_back(GPUBarrier::Buffer(&rcommon::buffers[BUFFERTYPE_FRAMECB], ResourceState::CONSTANT_BUFFER, ResourceState::COPY_DST));
		if (scene_Gdetails->instanceBuffer.IsValid())
		{
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->instanceBuffer, ResourceState::SHADER_RESOURCE, ResourceState::COPY_DST));
		}
		if (scene_Gdetails->geometryBuffer.IsValid())
		{
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->geometryBuffer, ResourceState::SHADER_RESOURCE, ResourceState::COPY_DST));
		}
		if (scene_Gdetails->materialBuffer.IsValid())
		{
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->materialBuffer, ResourceState::SHADER_RESOURCE, ResourceState::COPY_DST));
		}
		BarrierStackFlush(cmd);

		device->UpdateBuffer(&rcommon::buffers[BUFFERTYPE_FRAMECB], &frameCB, cmd);
		barrierStack.push_back(GPUBarrier::Buffer(&rcommon::buffers[BUFFERTYPE_FRAMECB], ResourceState::COPY_DST, ResourceState::CONSTANT_BUFFER));

		if (scene_Gdetails->instanceBuffer.IsValid() && scene_Gdetails->instanceArraySize > 0)
		{
			device->CopyBuffer(
				&scene_Gdetails->instanceBuffer,
				0,
				&scene_Gdetails->instanceUploadBuffer[device->GetBufferIndex()],
				0,
				scene_Gdetails->instanceArraySize * sizeof(ShaderMeshInstance),
				cmd
			);
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->instanceBuffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE));
		}

		if (scene_Gdetails->geometryBuffer.IsValid() && scene_Gdetails->geometryArraySize > 0)
		{
			device->CopyBuffer(
				&scene_Gdetails->geometryBuffer,
				0,
				&scene_Gdetails->geometryUploadBuffer[device->GetBufferIndex()],
				0,
				scene_Gdetails->geometryArraySize * sizeof(ShaderGeometry),
				cmd
			);
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->geometryBuffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE));
		}

		if (scene_Gdetails->materialBuffer.IsValid() && scene_Gdetails->materialArraySize > 0)
		{
			device->CopyBuffer(
				&scene_Gdetails->materialBuffer,
				0,
				&scene_Gdetails->materialUploadBuffer[device->GetBufferIndex()],
				0,
				scene_Gdetails->materialArraySize * sizeof(ShaderMaterial),
				cmd
			);
			barrierStack.push_back(GPUBarrier::Buffer(&scene_Gdetails->materialBuffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE));
		}

		//barrierStack.push_back(GPUBarrier::Image(&common::textures[TEXTYPE_2D_CAUSTICS], common::textures[TEXTYPE_2D_CAUSTICS].desc.layout, ResourceState::UNORDERED_ACCESS));

		// Flush buffer updates:
		BarrierStackFlush(cmd);

		profiler::EndRange(prof_updatebuffer_cpu);
		profiler::EndRange(prof_updatebuffer_gpu);

		BindCommonResources(cmd);

		//{
		//	//device->ClearUAV(&textures[TEXTYPE_2D_CAUSTICS], 0, cmd);
		//	device->Barrier(GPUBarrier::Memory(), cmd);
		//}
		//{
		//	auto range = profiler::BeginRangeGPU("Caustics", cmd);
		//	device->EventBegin("Caustics", cmd);
		//	device->BindComputeShader(&rcommon::shaders[CSTYPE_CAUSTICS], cmd);
		//	device->BindUAV(&textures[TEXTYPE_2D_CAUSTICS], 0, cmd);
		//	const TextureDesc& desc = textures[TEXTYPE_2D_CAUSTICS].GetDesc();
		//	device->Dispatch(desc.width / 8, desc.height / 8, 1, cmd);
		//	barrierStack.push_back(GPUBarrier::Image(&textures[TEXTYPE_2D_CAUSTICS], ResourceState::UNORDERED_ACCESS, textures[TEXTYPE_2D_CAUSTICS].desc.layout));
		//	device->EventEnd(cmd);
		//	profiler::EndRange(range);
		//}
		//
		//BarrierStackFlush(cmd); // wind/skinning flush

		device->EventEnd(cmd);
	}
	void GRenderPath3DDetails::UpdateRenderDataAsync(const View& view, const FrameCB& frameCB, CommandList cmd)
	{
		device->EventBegin("UpdateRenderDataAsync", cmd);

		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		BindCommonResources(cmd);

		// Wetmaps will be initialized:
		for (uint32_t renderableIndex = 0, n = (uint32_t)view.scene->GetRenderableCount(); renderableIndex < n; ++renderableIndex)
		{
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderableIndex];
			if (!renderable.IsMeshRenderable())
			{
				continue;
			}
			Entity geometry_entity = renderable.GetGeometry();
			GGeometryComponent& geomety = *(GGeometryComponent*)compfactory::GetGeometryComponent(geometry_entity);
			if (!geomety.HasRenderData())
			{
				continue;
			}

			size_t num_parts = geomety.GetNumParts();
			bool has_buffer_effect = num_parts == renderable.bufferEffects.size();
			for (size_t part_index = 0; part_index < num_parts; ++part_index)
			{
				if (!has_buffer_effect)
				{
					continue;
				}
				const GPrimEffectBuffers& prim_effect_buffers = renderable.bufferEffects[part_index];

				if (prim_effect_buffers.wetmapCleared || !prim_effect_buffers.wetmapBuffer.IsValid())
				{
					continue;
				}
				device->ClearUAV(&prim_effect_buffers.wetmapBuffer, 0, cmd);
				barrierStack.push_back(GPUBarrier::Buffer(&prim_effect_buffers.wetmapBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
				prim_effect_buffers.wetmapCleared = true;
			}
		}

		BarrierStackFlush(cmd);

		if (scene_Gdetails->textureStreamingFeedbackBuffer.IsValid())
		{
			device->ClearUAV(&scene_Gdetails->textureStreamingFeedbackBuffer, 0, cmd);
		}

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::OcclusionCulling_Reset(const View& view, CommandList cmd)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();
		const GPUQueryHeap& queryHeap = scene_Gdetails->queryHeap;

		if (!renderer::isOcclusionCullingEnabled || renderer::isFreezeCullingCameraEnabled || !queryHeap.IsValid())
		{
			return;
		}
		if (view.visibleRenderables.empty() && view.visibleLights.empty())
		{
			return;
		}

		device->QueryReset(
			&queryHeap,
			0,
			queryHeap.desc.query_count,
			cmd
		);
	}
	void GRenderPath3DDetails::OcclusionCulling_Render(const CameraComponent& camera, const View& view, CommandList cmd)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();
		const GPUQueryHeap& queryHeap = scene_Gdetails->queryHeap;

		if (!renderer::isOcclusionCullingEnabled || renderer::isFreezeCullingCameraEnabled || !queryHeap.IsValid())
		{
			return;
		}
		if (view.visibleRenderables.empty() && view.visibleLights.empty())
		{
			return;
		}

		auto range = profiler::BeginRangeGPU("Occlusion Culling Render", &cmd);

		device->BindPipelineState(&rcommon::PSO_occlusionquery, cmd);

		XMMATRIX VP = XMLoadFloat4x4(&camera.GetViewProjection());

		int query_write = scene_Gdetails->queryheapIdx;

		if (!view.visibleRenderables.empty())
		{
			device->EventBegin("Occlusion Culling Objects", cmd);

			for (uint32_t instanceIndex : view.visibleRenderables)
			{
				const GSceneDetails::OcclusionResult& occlusion_result = scene_Gdetails->occlusionResultsObjects[instanceIndex];
				GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];

				int queryIndex = occlusion_result.occlusionQueries[query_write];
				if (queryIndex >= 0)
				{
					const AABB& aabb = renderable.GetAABB();
					const XMMATRIX transform = aabb.getAsBoxMatrix() * VP;
					device->PushConstants(&transform, sizeof(transform), cmd);

					// render bounding box to later read the occlusion status
					device->QueryBegin(&queryHeap, queryIndex, cmd);
					device->Draw(14, 0, cmd);
					device->QueryEnd(&queryHeap, queryIndex, cmd);
				}
			}

			device->EventEnd(cmd);
		}

		if (!view.visibleLights.empty())
		{
			device->EventBegin("Occlusion Culling Lights", cmd);

			for (uint32_t lightIndex : view.visibleLights)
			{
				const LightComponent& light = *scene_Gdetails->lightComponents[lightIndex];

				if (light.occlusionquery >= 0)
				{
					uint32_t queryIndex = (uint32_t)light.occlusionquery;
					const AABB& aabb = light.GetAABB();
					const XMMATRIX transform = aabb.getAsBoxMatrix() * VP;
					device->PushConstants(&transform, sizeof(transform), cmd);

					device->QueryBegin(&queryHeap, queryIndex, cmd);
					device->Draw(14, 0, cmd);
					device->QueryEnd(&queryHeap, queryIndex, cmd);
				}
			}

			device->EventEnd(cmd);
		}

		profiler::EndRange(range); // Occlusion Culling Render
	}
	void GRenderPath3DDetails::OcclusionCulling_Resolve(const View& view, CommandList cmd)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();
		const GPUQueryHeap& queryHeap = scene_Gdetails->queryHeap;

		if (!renderer::isOcclusionCullingEnabled || renderer::isFreezeCullingCameraEnabled || !queryHeap.IsValid())
		{
			return;
		}
		if (view.visibleRenderables.empty() && view.visibleLights.empty())
		{
			return;
		}

		int query_write = scene_Gdetails->queryheapIdx;
		uint32_t queryCount = scene_Gdetails->queryAllocator.load();

		// Resolve into readback buffer:
		device->QueryResolve(
			&queryHeap,
			0,
			queryCount,
			&scene_Gdetails->queryResultBuffer[query_write],
			0ull,
			cmd
		);

		if (device->CheckCapability(GraphicsDeviceCapability::PREDICATION))
		{
			// Resolve into predication buffer:
			device->QueryResolve(
				&queryHeap,
				0,
				queryCount,
				&scene_Gdetails->queryPredicationBuffer,
				0ull,
				cmd
			);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&scene_Gdetails->queryPredicationBuffer, ResourceState::COPY_DST, ResourceState::PREDICATION),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}
		}
	}

	void GRenderPath3DDetails::RenderDirectVolumes(CommandList cmd)
	{
		if (viewMain.visibleRenderables.empty())
			return;

		GSceneDetails* scene_Gdetails = (GSceneDetails*)viewMain.scene->GetGSceneHandle();
		if (scene_Gdetails->renderableComponents_volume.empty())
		{
			return;
		}

		device->EventBegin("Direct Volume Render", cmd);
		auto range = profiler::BeginRangeGPU("DrawDirectVolume", &cmd);

		BindCommonResources(cmd);

		uint32_t filterMask = GMaterialComponent::FILTER_VOLUME;

		// Note: the tile_count here must be valid whether the ViewResources was created or not!
		XMUINT2 tile_count = GetViewTileCount(XMUINT2(rtMain.desc.width, rtMain.desc.height));

		GPUResource unbind;

		static thread_local RenderQueue renderQueue;
		renderQueue.init();
		for (uint32_t instanceIndex : viewMain.visibleRenderables)
		{
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];
			if (!renderable.IsVolumeRenderable())
				continue;
			if (!(viewMain.camera->GetVisibleLayerMask() & renderable.GetVisibleMask()))
				continue;
			if ((renderable.materialFilterFlags & filterMask) == 0)
				continue;

			const float distance = math::Distance(viewMain.camera->GetWorldEye(), renderable.GetAABB().getCenter());
			if (distance > renderable.GetFadeDistance() + renderable.GetAABB().getRadius())
				continue;

			renderQueue.add(renderable.geometryIndex, instanceIndex, distance, renderable.sortBits);
		}
		if (!renderQueue.empty())
		{
			// We use a policy where the closer it is to the front, the higher the priority.
			renderQueue.sort_opaque();
			//renderQueue.sort_transparent();
		}

		uint32_t instanceCount = 0;
		for (const RenderBatch& batch : renderQueue.batches) // Do not break out of this loop!
		{
			const uint32_t geometry_index = batch.GetGeometryIndex();	// geometry index
			const uint32_t renderable_index = batch.GetRenderableIndex();	// renderable index (base renderable)
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderable_index];
			assert(renderable.IsVolumeRenderable());

			GMaterialComponent* material = (GMaterialComponent*)compfactory::GetMaterialComponent(renderable.GetMaterial(0));
			assert(material);

			GVolumeComponent* volume = (GVolumeComponent*)compfactory::GetVolumeComponentByVUID(
				material->GetVolumeTextureVUID(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP));
			assert(volume);
			assert(volume->IsValidVolume());

			GTextureComponent* otf = (GTextureComponent*)compfactory::GetTextureComponentByVUID(
				material->GetLookupTableVUID(MaterialComponent::LookupTableSlot::LOOKUP_OTF));
			assert(otf);
			Entity entity_otf = otf->GetEntity();
			XMFLOAT2 tableValidBeginEndRatioX = otf->GetTableValidBeginEndRatioX();

			VolumePushConstants volume_push;
			{
				const XMFLOAT3& vox_size = volume->GetVoxelSize();
				volume_push.instanceIndex = batch.instanceIndex;
				volume_push.sculptStep = -1;
				volume_push.opacity_correction = 1.f;
				volume_push.main_visible_min_sample = tableValidBeginEndRatioX.x;

				const XMUINT3& block_pitch = volume->GetBlockPitch();
				XMFLOAT3 vol_size = XMFLOAT3((float)volume->GetWidth(), (float)volume->GetHeight(), (float)volume->GetDepth());
				volume_push.singleblock_size_ts = XMFLOAT3((float)block_pitch.x / vol_size.x,
					(float)block_pitch.y / vol_size.y, (float)block_pitch.z / vol_size.z);

				volume_push.mask_value_range = 255.f;
				const XMFLOAT2& min_max_stored_v = volume->GetStoredMinMax();
				volume_push.value_range = min_max_stored_v.y - min_max_stored_v.x;
				volume_push.mask_unormid_otf_map = volume_push.mask_value_range / (otf->GetHeight() > 1 ? otf->GetHeight() - 1 : 1.f);

				const XMUINT3& blocks_size = volume->GetBlocksSize();
				volume_push.blocks_w = blocks_size.x;
				volume_push.blocks_wh = blocks_size.x * blocks_size.y;
			}

			if (rtMain.IsValid() && rtLinearDepth.IsValid())
			{
				device->BindUAV(&rtMain, 0, cmd);
				device->BindUAV(&rtLinearDepth, 1, cmd, 0);
			}
			else
			{
				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
			}

			barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));
			BarrierStackFlush(cmd);

			device->BindComputeShader(&rcommon::shaders[CSTYPE_DVR_DEFAULT], cmd);
			device->PushConstants(&volume_push, sizeof(VolumePushConstants), cmd);

			device->Dispatch(
				tile_count.x,
				tile_count.y,
				1,
				cmd
			);

			device->BindUAV(&unbind, 0, cmd);
			device->BindUAV(&unbind, 1, cmd);

			barrierStack.push_back(GPUBarrier::Image(&rtMain, ResourceState::UNORDERED_ACCESS, rtMain.desc.layout));
			barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
			BarrierStackFlush(cmd);

			break; // TODO: at this moment, just a single volume is supported!
		}


		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::DrawScene(const View& view, RENDERPASS renderPass, CommandList cmd, uint32_t flags)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		const bool opaque = flags & DRAWSCENE_OPAQUE;
		const bool transparent = flags & DRAWSCENE_TRANSPARENT;
		const bool occlusion = (flags & DRAWSCENE_OCCLUSIONCULLING) && (view.flags & View::ALLOW_OCCLUSION_CULLING) && isOcclusionCullingEnabled;
		const bool foreground = flags & DRAWSCENE_FOREGROUND_ONLY;

		device->EventBegin("DrawScene", cmd);
		device->BindShadingRate(ShadingRate::RATE_1X1, cmd);

		BindCommonResources(cmd);

		uint32_t filterMask = 0;
		if (opaque)
		{
			filterMask |= GMaterialComponent::FILTER_OPAQUE;
		}
		if (transparent)
		{
			filterMask |= GMaterialComponent::FILTER_TRANSPARENT;
		}

		if (isWireRender)
		{
			filterMask = GMaterialComponent::FILTER_ALL;
		}

		if (opaque || transparent)
		{
			static thread_local RenderQueue renderQueue;
			renderQueue.init();
			for (uint32_t instanceIndex : view.visibleRenderables)
			{
				if (occlusion && scene_Gdetails->occlusionResultsObjects[instanceIndex].IsOccluded())
					continue;
				
				const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];
				if (!renderable.IsMeshRenderable())
					continue;
				if (foreground != renderable.IsForeground())
					continue;
				if (!(view.camera->GetVisibleLayerMask() & renderable.GetVisibleMask()))
					continue;
				if ((renderable.materialFilterFlags & filterMask) == 0)
					continue;

				const float distance = math::Distance(view.camera->GetWorldEye(), renderable.GetAABB().getCenter());
				if (distance > renderable.GetFadeDistance() + renderable.GetAABB().getRadius())
					continue;

				renderQueue.add(renderable.geometryIndex, instanceIndex, distance, renderable.sortBits);
			}
			if (!renderQueue.empty())
			{
				if (transparent)
				{
					renderQueue.sort_transparent();
				}
				else
				{
					renderQueue.sort_opaque();
				}
				RenderMeshes(view, renderQueue, renderPass, filterMask, cmd, flags);
			}
		}

		device->BindShadingRate(ShadingRate::RATE_1X1, cmd);
		device->EventEnd(cmd);

	}
}

namespace vz
{
	void GRenderPath3DDetails::Postprocess_Tonemap(
		const Texture& input,
		const Texture& output,
		CommandList cmd,
		float exposure,
		float brightness,
		float contrast,
		float saturation,
		bool dither,
		const Texture* texture_colorgradinglut,
		const Texture* texture_distortion,
		const GPUBuffer* buffer_luminance,
		const Texture* texture_bloom,
		ColorSpace display_colorspace,
		Tonemap tonemap,
		const Texture* texture_distortion_overlay,
		float hdr_calibration
	)
	{
		if (!input.IsValid() || !output.IsValid())
		{
			assert(0);
			return;
		}

		device->EventBegin("Postprocess_Tonemap", cmd);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&output, output.desc.layout, ResourceState::UNORDERED_ACCESS),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->ClearUAV(&output, 0, cmd);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Memory(&output),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->BindComputeShader(&rcommon::shaders[CSTYPE_POSTPROCESS_TONEMAP], cmd);

		const TextureDesc& desc = output.GetDesc();

		assert(texture_colorgradinglut == nullptr || texture_colorgradinglut->desc.type == TextureDesc::Type::TEXTURE_3D); // This must be a 3D lut

		XMHALF4 exposure_brightness_contrast_saturation = XMHALF4(exposure, brightness, contrast, saturation);

		PushConstantsTonemap tonemap_push = {};
		tonemap_push.resolution_rcp.x = 1.0f / desc.width;
		tonemap_push.resolution_rcp.y = 1.0f / desc.height;
		tonemap_push.exposure_brightness_contrast_saturation.x = uint(exposure_brightness_contrast_saturation.v);
		tonemap_push.exposure_brightness_contrast_saturation.y = uint(exposure_brightness_contrast_saturation.v >> 32ull);
		tonemap_push.flags_hdrcalibration = 0;
		if (dither)
		{
			tonemap_push.flags_hdrcalibration |= TONEMAP_FLAG_DITHER;
		}
		if (tonemap == Tonemap::ACES)
		{
			tonemap_push.flags_hdrcalibration |= TONEMAP_FLAG_ACES;
		}
		if (display_colorspace == ColorSpace::SRGB)
		{
			tonemap_push.flags_hdrcalibration |= TONEMAP_FLAG_SRGB;
		}
		tonemap_push.flags_hdrcalibration |= XMConvertFloatToHalf(hdr_calibration) << 16u;
		tonemap_push.texture_input = device->GetDescriptorIndex(&input, SubresourceType::SRV);
		tonemap_push.buffer_input_luminance = device->GetDescriptorIndex((buffer_luminance == nullptr) ? &rcommon::luminanceDummy : buffer_luminance, SubresourceType::SRV);
		tonemap_push.texture_input_distortion = device->GetDescriptorIndex(texture_distortion, SubresourceType::SRV);
		tonemap_push.texture_input_distortion_overlay = device->GetDescriptorIndex(texture_distortion_overlay, SubresourceType::SRV);
		tonemap_push.texture_colorgrade_lookuptable = device->GetDescriptorIndex(texture_colorgradinglut, SubresourceType::SRV);
		tonemap_push.texture_bloom = device->GetDescriptorIndex(texture_bloom, SubresourceType::SRV);
		tonemap_push.texture_output = device->GetDescriptorIndex(&output, SubresourceType::UAV);
		device->PushConstants(&tonemap_push, sizeof(tonemap_push), cmd);

		device->Dispatch(
			(desc.width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
			(desc.height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
			1,
			cmd
		);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&output, ResourceState::UNORDERED_ACCESS, output.desc.layout),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}


		device->EventEnd(cmd);
	}
}

namespace vz
{
	void GRenderPath3DDetails::RefreshLightmaps(const Scene& scene, CommandList cmd)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)scene.GetGSceneHandle();

		// TODO for lightmap_request_allocator
		/*
		const uint32_t lightmap_request_count = lightmapRequestAllocator.load();
		if (lightmap_request_count > 0)
		{
			auto range = profiler::BeginRangeGPU("Lightmap Processing", cmd);

			if (!scene.TLAS.IsValid() && !scene.BVH.IsValid())
				return;

			jobsystem::Wait(raytracing_ctx);

			BindCommonResources(cmd);

			// Render lightmaps for each object:
			for (uint32_t requestIndex = 0; requestIndex < lightmap_request_count; ++requestIndex)
			{
				uint32_t objectIndex = *(scene.lightmap_requests.data() + requestIndex);
				const ObjectComponent& object = scene.objects[objectIndex];
				if (!object.lightmap.IsValid())
					continue;

				if (object.IsLightmapRenderRequested())
				{
					device->EventBegin("RenderObjectLightMap", cmd);

					const MeshComponent& mesh = scene.meshes[object.mesh_index];
					assert(!mesh.vertex_atlas.empty());
					assert(mesh.vb_atl.IsValid());

					const TextureDesc& desc = object.lightmap.GetDesc();

					if (object.lightmapIterationCount == 0)
					{
						RenderPassImage rp = RenderPassImage::RenderTarget(&object.lightmap, RenderPassImage::LoadOp::CLEAR);
						device->RenderPassBegin(&rp, 1, cmd);
					}
					else
					{
						RenderPassImage rp = RenderPassImage::RenderTarget(&object.lightmap, RenderPassImage::LoadOp::LOAD);
						device->RenderPassBegin(&rp, 1, cmd);
					}

					Viewport vp;
					vp.width = (float)desc.width;
					vp.height = (float)desc.height;
					device->BindViewports(1, &vp, cmd);

					device->BindPipelineState(&PSO_renderlightmap, cmd);

					device->BindIndexBuffer(&mesh.generalBuffer, mesh.GetIndexFormat(), mesh.ib.offset, cmd);

					LightmapPushConstants push;
					push.vb_pos_w = mesh.vb_pos_wind.descriptor_srv;
					push.vb_nor = mesh.vb_nor.descriptor_srv;
					push.vb_atl = mesh.vb_atl.descriptor_srv;
					push.instanceIndex = objectIndex;
					device->PushConstants(&push, sizeof(push), cmd);

					RaytracingCB cb;
					cb.xTraceResolution.x = desc.width;
					cb.xTraceResolution.y = desc.height;
					cb.xTraceResolution_rcp.x = 1.0f / cb.xTraceResolution.x;
					cb.xTraceResolution_rcp.y = 1.0f / cb.xTraceResolution.y;
					XMFLOAT4 halton = math::GetHaltonSequence(object.lightmapIterationCount); // for jittering the rasterization (good for eliminating atlas border artifacts)
					cb.xTracePixelOffset.x = (halton.x * 2 - 1) * cb.xTraceResolution_rcp.x;
					cb.xTracePixelOffset.y = (halton.y * 2 - 1) * cb.xTraceResolution_rcp.y;
					cb.xTracePixelOffset.x *= 1.4f;	// boost the jitter by a bit
					cb.xTracePixelOffset.y *= 1.4f;	// boost the jitter by a bit
					cb.xTraceAccumulationFactor = 1.0f / (object.lightmapIterationCount + 1.0f); // accumulation factor (alpha)
					cb.xTraceUserData.x = raytraceBounceCount;
					uint8_t instanceInclusionMask = 0xFF;
					cb.xTraceUserData.y = instanceInclusionMask;
					cb.xTraceSampleIndex = object.lightmapIterationCount;
					device->BindDynamicConstantBuffer(cb, CB_GETBINDSLOT(RaytracingCB), cmd);

					uint32_t first_subset = 0;
					uint32_t last_subset = 0;
					mesh.GetLODSubsetRange(0, first_subset, last_subset);
					for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
					{
						const MeshComponent::MeshSubset& subset = mesh.subsets[subsetIndex];
						if (subset.indexCount == 0)
							continue;
						device->DrawIndexed(subset.indexCount, subset.indexOffset, 0, cmd);
					}
					object.lightmapIterationCount++;

					device->RenderPassEnd(cmd);

					device->EventEnd(cmd);
				}
			}

			profiler::EndRange(range);
		}
		/**/
	}

	void GRenderPath3DDetails::RefreshWetmaps(const View& view, CommandList cmd)
	{
		return; // this will be useful for wetmap simulation for rainny weather...

		device->EventBegin("RefreshWetmaps", cmd);
		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		BindCommonResources(cmd);
		device->BindComputeShader(&rcommon::shaders[CSTYPE_WETMAP_UPDATE], cmd);

		WetmapPush push = {};
		push.wet_amount = 1.f;

		// Note: every object wetmap is updated, not just visible
		for (uint32_t renderableIndex = 0, n = (uint32_t)view.scene->GetRenderableCount(); renderableIndex < n; ++renderableIndex)
		{
			push.instanceIndex = renderableIndex;
			GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderableIndex];

			if (!renderable.IsMeshRenderable())
			{
				continue;
			}

			Entity geometry_entity = renderable.GetGeometry();
			GGeometryComponent& geometry = *(GGeometryComponent*)compfactory::GetGeometryComponent(geometry_entity);

			std::vector<Entity> materials(renderable.GetNumParts());
			assert(renderable.GetNumParts() == renderable.bufferEffects.size());
			renderable.GetMaterials(materials.data());
			for (size_t part_index = 0, n = renderable.bufferEffects.size(); part_index < n; ++part_index)
			{
				GPrimEffectBuffers& prim_effect_buffers = renderable.bufferEffects[part_index];
				GMaterialComponent& material = *(GMaterialComponent*)compfactory::GetMaterialComponent(materials[part_index]);
				if (!material.IsWetmapEnabled() && prim_effect_buffers.wetmapBuffer.IsValid())
					continue;
				uint32_t vertex_count = uint32_t(prim_effect_buffers.wetmapBuffer.desc.size 
					/ GetFormatStride(prim_effect_buffers.wetmapBuffer.desc.format));
				push.wetmap = device->GetDescriptorIndex(&prim_effect_buffers.wetmapBuffer, SubresourceType::UAV);
				if (push.wetmap < 0)
					continue;

				push.subsetIndex = part_index;;

				device->PushConstants(&push, sizeof(push), cmd);
				device->Dispatch((vertex_count + 63u) / 64u, 1, 1, cmd);
			}
		}

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::TextureStreamingReadbackCopy(const Scene& scene, graphics::CommandList cmd)
	{
		GSceneDetails* scene_Gdetails = (GSceneDetails*)scene.GetGSceneHandle();
		if (scene_Gdetails->textureStreamingFeedbackBuffer.IsValid())
		{
			device->Barrier(GPUBarrier::Buffer(&scene_Gdetails->textureStreamingFeedbackBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SRC), cmd);
			device->CopyResource(
				&scene_Gdetails->textureStreamingFeedbackBuffer_readback[device->GetBufferIndex()],
				&scene_Gdetails->textureStreamingFeedbackBuffer,
				cmd
			);
		}
	}
}

namespace vz
{
	void GRenderPath3DDetails::CreateTiledLightResources(TiledLightResources& res, XMUINT2 resolution)
	{
		res.tileCount = GetEntityCullingTileCount(resolution);

		GPUBufferDesc bd;
		bd.stride = sizeof(uint);
		bd.size = res.tileCount.x * res.tileCount.y * bd.stride * SHADER_ENTITY_TILE_BUCKET_COUNT * 2; // *2: opaque and transparent arrays
		bd.usage = Usage::DEFAULT;
		bd.bind_flags = BindFlag::UNORDERED_ACCESS | BindFlag::SHADER_RESOURCE;
		bd.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
		device->CreateBuffer(&bd, nullptr, &res.entityTiles);
		device->SetName(&res.entityTiles, "entityTiles");
	}

	void GRenderPath3DDetails::ComputeTiledLightCulling(
		const TiledLightResources& res,
		const View& vis,
		const Texture& debugUAV,
		CommandList cmd
	)
	{
		auto range = profiler::BeginRangeGPU("Entity Culling", &cmd);

		device->Barrier(GPUBarrier::Buffer(&res.entityTiles, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS), cmd);

		if (
			vis.visibleLights.empty() //&&
			//vis.visibleDecals.empty() &&
			//vis.visibleEnvProbes.empty()
			)
		{
			device->EventBegin("Tiled Entity Clear Only", cmd);
			device->ClearUAV(&res.entityTiles, 0, cmd);
			device->Barrier(GPUBarrier::Buffer(&res.entityTiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE), cmd);
			device->EventEnd(cmd);
			profiler::EndRange(range);
			return;
		}

		BindCommonResources(cmd);

		// Perform the culling
		{
			device->EventBegin("Entity Culling", cmd);

			if (isDebugLightCulling && debugUAV.IsValid())
			{
				device->BindComputeShader(&rcommon::shaders[isAdvancedLightCulling ? CSTYPE_LIGHTCULLING_ADVANCED_DEBUG : CSTYPE_LIGHTCULLING_DEBUG], cmd);
				device->BindUAV(&debugUAV, 3, cmd);
			}
			else
			{
				device->BindComputeShader(&rcommon::shaders[isAdvancedLightCulling ? CSTYPE_LIGHTCULLING_ADVANCED : CSTYPE_LIGHTCULLING], cmd);
			}

			const GPUResource* uavs[] = {
				&res.entityTiles,
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			device->Dispatch(res.tileCount.x, res.tileCount.y, 1, cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&res.entityTiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);
		}

		// Unbind from UAV slots:
		GPUResource empty;
		const GPUResource* uavs[] = {
			&empty,
			&empty
		};
		device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

		profiler::EndRange(range);
	}
}

namespace vz
{
	void GRenderPath3DDetails::CreateViewResources(ViewResources& res, XMUINT2 resolution)
	{
		res.tile_count = GetViewTileCount(resolution);
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderTypeBin);
			desc.size = desc.stride * (SCU32(MaterialComponent::ShaderType::COUNT) + 1); // +1 for sky
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED | ResourceMiscFlag::INDIRECT_ARGS;
			bool success = device->CreateBuffer(&desc, nullptr, &res.bins);
			assert(success);
			device->SetName(&res.bins, "res.bins");

			desc.stride = sizeof(ViewTile);
			desc.size = desc.stride * res.tile_count.x * res.tile_count.y * (SCU32(MaterialComponent::ShaderType::COUNT) + 1); // +1 for sky
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			success = device->CreateBuffer(&desc, nullptr, &res.binned_tiles);
			assert(success);
			device->SetName(&res.binned_tiles, "res.binned_tiles");
		}
		{
			TextureDesc desc;
			desc.width = resolution.x;
			desc.height = resolution.y;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;

			desc.format = Format::R16G16_FLOAT;
			device->CreateTexture(&desc, nullptr, &res.texture_normals);
			device->SetName(&res.texture_normals, "res.texture_normals");

			desc.format = Format::R8_UNORM;
			device->CreateTexture(&desc, nullptr, &res.texture_roughness);
			device->SetName(&res.texture_roughness, "res.texture_roughness");

			desc.format = Format::R32G32B32A32_UINT;
			device->CreateTexture(&desc, nullptr, &res.texture_payload_0);
			device->SetName(&res.texture_payload_0, "res.texture_payload_0");
			device->CreateTexture(&desc, nullptr, &res.texture_payload_1);
			device->SetName(&res.texture_payload_1, "res.texture_payload_1");
		}
	}

	void GRenderPath3DDetails::View_Prepare(
		const ViewResources& res,
		const Texture& input_primitiveID_1, // can be MSAA
		const Texture& input_primitiveID_2, // can be MSAA
		CommandList cmd
	)
	{
		device->EventBegin("View_Prepare", cmd);
		auto range = profiler::BeginRangeGPU("View_Prepare", &cmd);

		BindCommonResources(cmd);

		// Note: the tile_count here must be valid whether the ViewResources was created or not!
		XMUINT2 tile_count = GetViewTileCount(XMUINT2(input_primitiveID_1.desc.width, input_primitiveID_1.desc.height));

		// Beginning barriers, clears:
		if (res.IsValid())
		{
			ShaderTypeBin bins[SHADERTYPE_BIN_COUNT + 1];
			for (uint i = 0; i < arraysize(bins); ++i)
			{
				ShaderTypeBin& bin = bins[i];
				bin.dispatchX = 0; // will be used for atomic add in shader
				bin.dispatchY = 1;
				bin.dispatchZ = 1;
				bin.shaderType = i;
			}
			device->UpdateBuffer(&res.bins, bins, cmd);
			barrierStack.push_back(GPUBarrier::Buffer(&res.bins, ResourceState::COPY_DST, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Buffer(&res.binned_tiles, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
			BarrierStackFlush(cmd);
		}

		// Resolve:
		//	PrimitiveID -> depth, lineardepth
		//	Binning classification
		{
			device->EventBegin("Resolve", cmd);
			const bool msaa = input_primitiveID_1.GetDesc().sample_count > 1;

			device->BindResource(&input_primitiveID_1, 0, cmd);
			device->BindResource(&input_primitiveID_2, 1, cmd);

			GPUResource unbind;

			if (res.IsValid())
			{
				device->BindUAV(&res.bins, 0, cmd);
				device->BindUAV(&res.binned_tiles, 1, cmd);
			}
			else
			{
				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
			}

			if (res.depthbuffer)
			{
				device->BindUAV(res.depthbuffer, 3, cmd, 0);
				device->BindUAV(res.depthbuffer, 4, cmd, 1);
				device->BindUAV(res.depthbuffer, 5, cmd, 2);
				device->BindUAV(res.depthbuffer, 6, cmd, 3);
				device->BindUAV(res.depthbuffer, 7, cmd, 4);
				barrierStack.push_back(GPUBarrier::Image(res.depthbuffer, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
			}
			else
			{
				device->BindUAV(&unbind, 3, cmd);
				device->BindUAV(&unbind, 4, cmd);
				device->BindUAV(&unbind, 5, cmd);
				device->BindUAV(&unbind, 6, cmd);
				device->BindUAV(&unbind, 7, cmd);
			}
			if (res.lineardepth)
			{
				device->BindUAV(res.lineardepth, 8, cmd, 0);
				device->BindUAV(res.lineardepth, 9, cmd, 1);
				device->BindUAV(res.lineardepth, 10, cmd, 2);
				device->BindUAV(res.lineardepth, 11, cmd, 3);
				device->BindUAV(res.lineardepth, 12, cmd, 4);
				barrierStack.push_back(GPUBarrier::Image(res.lineardepth, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
			}
			else
			{
				device->BindUAV(&unbind, 8, cmd);
				device->BindUAV(&unbind, 9, cmd);
				device->BindUAV(&unbind, 10, cmd);
				device->BindUAV(&unbind, 11, cmd);
				device->BindUAV(&unbind, 12, cmd);
			}
			if (res.primitiveID_1_resolved)
			{
				device->BindUAV(res.primitiveID_1_resolved, 13, cmd);
				device->BindUAV(res.primitiveID_2_resolved, 14, cmd);
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_1_resolved, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_2_resolved, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
			}
			else
			{
				device->BindUAV(&unbind, 13, cmd);
				device->BindUAV(&unbind, 14, cmd);
			}
			BarrierStackFlush(cmd);

			device->BindComputeShader(&rcommon::shaders[msaa ? CSTYPE_VIEW_RESOLVE_MSAA : CSTYPE_VIEW_RESOLVE], cmd);

			device->Dispatch(
				tile_count.x,
				tile_count.y,
				1,
				cmd
			);

			if (res.depthbuffer)
			{
				barrierStack.push_back(GPUBarrier::Image(res.depthbuffer, ResourceState::UNORDERED_ACCESS, res.depthbuffer->desc.layout));
			}
			if (res.lineardepth)
			{
				barrierStack.push_back(GPUBarrier::Image(res.lineardepth, ResourceState::UNORDERED_ACCESS, res.lineardepth->desc.layout));
			}
			if (res.primitiveID_1_resolved)
			{
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_1_resolved, ResourceState::UNORDERED_ACCESS, res.primitiveID_1_resolved->desc.layout));
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_2_resolved, ResourceState::UNORDERED_ACCESS, res.primitiveID_2_resolved->desc.layout));
			}
			if (res.IsValid())
			{
				barrierStack.push_back(GPUBarrier::Buffer(&res.bins, ResourceState::UNORDERED_ACCESS, ResourceState::INDIRECT_ARGUMENT));
				barrierStack.push_back(GPUBarrier::Buffer(&res.binned_tiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
			}
			BarrierStackFlush(cmd);

			device->EventEnd(cmd);
		}

		profiler::EndRange(range);
		
		device->EventEnd(cmd);
	}
	
	void GRenderPath3DDetails::View_Surface(
		const ViewResources& res,
		const Texture& output,
		CommandList cmd
	)
	{
		device->EventBegin("View_Surface", cmd);
		auto range = profiler::BeginRangeGPU("View_Surface", &cmd);

		BindCommonResources(cmd);

		// First, do a bunch of resource discards to initialize texture metadata:
		barrierStack.push_back(GPUBarrier::Image(&output, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_normals, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_roughness, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_payload_0, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_payload_1, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
		BarrierStackFlush(cmd);

		device->BindResource(&res.binned_tiles, 0, cmd);
		device->BindUAV(&output, 0, cmd);
		device->BindUAV(&res.texture_normals, 1, cmd);
		device->BindUAV(&res.texture_roughness, 2, cmd);
		device->BindUAV(&res.texture_payload_0, 3, cmd);
		device->BindUAV(&res.texture_payload_1, 4, cmd);

		const uint visibility_tilecount_flat = res.tile_count.x * res.tile_count.y;
		uint visibility_tile_offset = 0;

		// surface dispatches per material type:
		device->EventBegin("Surface parameters", cmd);
		for (uint i = 0; i < SHADERTYPE_BIN_COUNT; ++i)
		{
			device->BindComputeShader(&rcommon::shaders[CSTYPE_VIEW_SURFACE_PERMUTATION__BEGIN + i], cmd);
			device->PushConstants(&visibility_tile_offset, sizeof(visibility_tile_offset), cmd);
			device->DispatchIndirect(&res.bins, i * sizeof(ShaderTypeBin) + offsetof(ShaderTypeBin, dispatchX), cmd);
			visibility_tile_offset += visibility_tilecount_flat;
		}
		device->EventEnd(cmd);

		// Ending barriers:
		//	These resources will be used by other post processing effects
		barrierStack.push_back(GPUBarrier::Image(&res.texture_normals, ResourceState::UNORDERED_ACCESS, res.texture_normals.desc.layout));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_roughness, ResourceState::UNORDERED_ACCESS, res.texture_roughness.desc.layout));
		BarrierStackFlush(cmd);

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
	void GRenderPath3DDetails::View_Surface_Reduced(
		const ViewResources& res,
		CommandList cmd
	)
	{
		assert(0 && "Not Yet Supported!");
		device->EventBegin("View_Surface_Reduced", cmd);
		auto range = profiler::BeginRangeGPU("View_Surface_Reduced", &cmd);

		BindCommonResources(cmd);

		barrierStack.push_back(GPUBarrier::Image(&res.texture_normals, res.texture_normals.desc.layout, ResourceState::UNORDERED_ACCESS));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_roughness, res.texture_roughness.desc.layout, ResourceState::UNORDERED_ACCESS));
		BarrierStackFlush(cmd);

		device->BindResource(&res.binned_tiles, 0, cmd);
		device->BindUAV(&res.texture_normals, 1, cmd);
		device->BindUAV(&res.texture_roughness, 2, cmd);

		const uint visibility_tilecount_flat = res.tile_count.x * res.tile_count.y;
		uint visibility_tile_offset = 0;

		// surface dispatches per material type:
		device->EventBegin("Surface parameters", cmd);
		for (uint i = 0; i < SHADERTYPE_BIN_COUNT; ++i)
		{
			if (i != SCU32(MaterialComponent::ShaderType::UNLIT)) // this won't need surface parameter write out
			{
				device->BindComputeShader(&rcommon::shaders[CSTYPE_VIEW_SURFACE_REDUCED_PERMUTATION__BEGIN + i], cmd);
				device->PushConstants(&visibility_tile_offset, sizeof(visibility_tile_offset), cmd);
				device->DispatchIndirect(&res.bins, i * sizeof(ShaderTypeBin) + offsetof(ShaderTypeBin, dispatchX), cmd);
			}
			visibility_tile_offset += visibility_tilecount_flat;
		}
		device->EventEnd(cmd);

		// Ending barriers:
		//	These resources will be used by other post processing effects
		barrierStack.push_back(GPUBarrier::Image(&res.texture_normals, ResourceState::UNORDERED_ACCESS, res.texture_normals.desc.layout));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_roughness, ResourceState::UNORDERED_ACCESS, res.texture_roughness.desc.layout));
		BarrierStackFlush(cmd);

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
	void GRenderPath3DDetails::View_Shade(
		const ViewResources& res,
		const Texture& output,
		CommandList cmd
	)
	{
		device->EventBegin("View_Shade", cmd);
		auto range = profiler::BeginRangeGPU("View_Shade", &cmd);

		BindCommonResources(cmd);

		barrierStack.push_back(GPUBarrier::Image(&res.texture_payload_0, ResourceState::UNORDERED_ACCESS, res.texture_payload_0.desc.layout));
		barrierStack.push_back(GPUBarrier::Image(&res.texture_payload_1, ResourceState::UNORDERED_ACCESS, res.texture_payload_1.desc.layout));
		BarrierStackFlush(cmd);

		device->BindResource(&res.binned_tiles, 0, cmd);
		device->BindResource(&res.texture_payload_0, 2, cmd);
		device->BindResource(&res.texture_payload_1, 3, cmd);
		device->BindUAV(&output, 0, cmd);

		const uint visibility_tilecount_flat = res.tile_count.x * res.tile_count.y;
		uint visibility_tile_offset = 0;

		// shading dispatches per material type:
		for (uint i = 0; i < SHADERTYPE_BIN_COUNT; ++i)
		{
			if (i != SCU32(MaterialComponent::ShaderType::UNLIT)) // the unlit shader is special, it had already written out its final color in the surface shader
			{
				device->BindComputeShader(&rcommon::shaders[CSTYPE_VIEW_SHADE_PERMUTATION__BEGIN + i], cmd);
				device->PushConstants(&visibility_tile_offset, sizeof(visibility_tile_offset), cmd);
				device->DispatchIndirect(&res.bins, i * sizeof(ShaderTypeBin) + offsetof(ShaderTypeBin, dispatchX), cmd);
			}
			visibility_tile_offset += visibility_tilecount_flat;
		}

		barrierStack.push_back(GPUBarrier::Image(&output, ResourceState::UNORDERED_ACCESS, output.desc.layout));
		BarrierStackFlush(cmd);

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
}

namespace vz
{
	void GRenderPath3DDetails::RenderMeshes(const View& view, const RenderQueue& renderQueue, RENDERPASS renderPass, uint32_t filterMask, CommandList cmd, uint32_t flags, uint32_t camera_count)
	{
		if (renderQueue.empty())
			return;

		GSceneDetails* scene_Gdetails = (GSceneDetails*)view.scene->GetGSceneHandle();

		device->EventBegin("RenderMeshes", cmd);

		// Always wait for non-mesh shader variants, it can be used when mesh shader is not applicable for an object:
		jobsystem::Wait(rcommon::CTX_renderPSO[renderPass][MESH_SHADER_PSO_DISABLED]);

		RenderPassInfo renderpass_info = device->GetRenderPassInfo(cmd);

		const bool tessellation =
			(flags & DRAWSCENE_TESSELLATION) &&
			isTessellationEnabled &&
			device->CheckCapability(GraphicsDeviceCapability::TESSELLATION)
			;

		// Do we need to compute a light mask for this pass on the CPU?
		const bool forward_lightmask_request =
			renderPass == RENDERPASS_ENVMAPCAPTURE ||
			renderPass == RENDERPASS_VOXELIZE;

		const bool shadow_rendering = renderPass == RENDERPASS_SHADOW;

		const bool mesh_shader = isMeshShaderAllowed &&
			(renderPass == RENDERPASS_PREPASS || renderPass == RENDERPASS_PREPASS_DEPTHONLY || renderPass == RENDERPASS_MAIN || renderPass == RENDERPASS_SHADOW);

		if (mesh_shader)
		{
			assert(0 && "NOT SUPPORTED YET!!");
			// Mesh shader is optional, only wait for these completions if enabled:
			jobsystem::Wait(rcommon::CTX_renderPSO[renderPass][MESH_SHADER_PSO_ENABLED]);
		}

		// Pre-allocate space for all the instances in GPU-buffer:
		const size_t alloc_size = renderQueue.size() * camera_count * sizeof(ShaderMeshInstancePointer);
		const GraphicsDevice::GPUAllocation instances = device->AllocateGPU(alloc_size, cmd);
		const int instanceBufferDescriptorIndex = device->GetDescriptorIndex(&instances.buffer, SubresourceType::SRV);

		// This will correspond to a single draw call
		//	It's used to render multiple instances of a single mesh
		//	Simply understand this as 'instances' originated from a renderable
		struct InstancedBatch
		{
			uint32_t geometryIndex = ~0u;	// geometryIndex
			uint32_t renderableIndex = ~0u;
			std::vector<uint32_t> materialIndices;
			uint32_t instanceCount = 0;	// 
			uint32_t dataOffset = 0;
			bool forceAlphatestForDithering = false;
			AABB aabb;
			uint32_t lod = 0;
		} instancedBatch = {};

		uint32_t prev_stencilref = SCU32(MaterialComponent::StencilRef::STENCILREF_DEFAULT);
		device->BindStencilRef(prev_stencilref, cmd);

		const GPUBuffer* prev_ib = nullptr;

		// This will be called every time we start a new draw call:
		//	calls draw per a geometry part
		auto BatchDrawingFlush = [&]()
			{
				if (instancedBatch.instanceCount == 0)
					return;
				GGeometryComponent& geometry = *scene_Gdetails->geometryComponents[instancedBatch.geometryIndex];

				if (!geometry.HasRenderData())
					return;

				GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instancedBatch.renderableIndex];

				bool forceAlphaTestForDithering = instancedBatch.forceAlphatestForDithering != 0;

				const float tessF = geometry.GetTessellationFactor();
				const bool tessellatorRequested = tessF > 0 && tessellation;
				const bool meshShaderRequested = !tessellatorRequested && mesh_shader;//&& geometry.vb_clu.IsValid();
				assert(!meshShaderRequested);

				if (forward_lightmask_request)
				{
					assert(0 && "Not Yet Supported!");
					//ForwardEntityMaskCB cb = ForwardEntityCullingCPU(view, instancedBatch.aabb, renderPass);
					//device->BindDynamicConstantBuffer(cb, CB_GETBINDSLOT(ForwardEntityMaskCB), cmd);
				}

				// Note: geometries and materials are scanned resources from the scene.	

				const std::vector<Primitive>& parts = geometry.GetPrimitives();
				assert(parts.size() == instancedBatch.materialIndices.size());
				for (uint32_t part_index = 0, num_parts = parts.size(); part_index < num_parts; ++part_index)
				{
					const Primitive& part = parts[part_index];
					GPrimBuffers& part_buffer = *(GPrimBuffers*)geometry.GetGPrimBuffer(part_index);

					uint32_t material_index = instancedBatch.materialIndices[part_index];
					const GMaterialComponent& material = *scene_Gdetails->materialComponents[material_index];

					
					if (material.GetAlphaRef() < 1)
					{
						forceAlphaTestForDithering = 1;
					}

					//if (skip_planareflection_objects && material.HasPlanarReflection())
					//	continue;

					bool is_renderable = filterMask & material.GetFilterMaskFlags();

					if (shadow_rendering)
					{
						is_renderable = is_renderable && material.IsCastShadow();
					}

					if (!is_renderable)
					{
						continue;
					}

					const PipelineState* pso = nullptr;
					const PipelineState* pso_backside = nullptr; // only when separate backside rendering is required (transparent doublesided)
					{
						if (isWireRender && renderPass != RENDERPASS_ENVMAPCAPTURE)
						{
							switch (renderPass)
							{
							case RENDERPASS_MAIN:
								if (meshShaderRequested)
								{
									assert(0 && "NOT YET SUPPORTED");
									//pso = &PSO_object_wire_mesh_shader;
								}
								else
								{
									//pso = tessellatorRequested ? &PSO_object_wire_tessellation : &rcommon::PSO_wireframe;
									pso = &rcommon::PSO_wireframe;
								}
							}
						}
						//else if (material.customShaderID >= 0 && material.customShaderID < (int)customShaders.size())
						//{
						//	const CustomShader& customShader = customShaders[material.customShaderID];
						//	if (filterMask & customShader.filterMask)
						//	{
						//		pso = &customShader.pso[renderPass];
						//	}
						//}
						else
						{
							MeshRenderingVariant variant = {};
							variant.bits.renderpass = renderPass;
							variant.bits.shadertype = SCU32(material.GetShaderType());
							variant.bits.blendmode = SCU32(material.GetBlendMode());
							variant.bits.cullmode = material.IsDoubleSided() ? (uint32_t)CullMode::NONE : (uint32_t)CullMode::BACK;
							variant.bits.tessellation = tessellatorRequested;
							variant.bits.alphatest = material.IsAlphaTestEnabled() || forceAlphaTestForDithering;
							variant.bits.sample_count = renderpass_info.sample_count;
							variant.bits.mesh_shader = meshShaderRequested;

							pso = shader::GetObjectPSO(variant);
							assert(pso->IsValid());

							if ((filterMask & GMaterialComponent::FILTER_TRANSPARENT) && variant.bits.cullmode == (uint32_t)CullMode::NONE)
							{
								variant.bits.cullmode = (uint32_t)CullMode::FRONT;
								pso_backside = shader::GetObjectPSO(variant);
							}
						}
					}

					if (pso == nullptr || !pso->IsValid())
					{
						continue;
					}

					const bool is_meshshader_pso = pso->desc.ms != nullptr;
					uint32_t stencilRef = CombineStencilrefs(material.GetStencilRef(), material.userStencilRef);
					if (stencilRef != prev_stencilref)
					{
						prev_stencilref = stencilRef;
						device->BindStencilRef(stencilRef, cmd);
					}

					if (!is_meshshader_pso && prev_ib != &part_buffer.generalBuffer)
					{
						device->BindIndexBuffer(&part_buffer.generalBuffer, geometry.GetIndexFormat(part_index), part_buffer.ib.offset, cmd);
						prev_ib = &part_buffer.generalBuffer;
					}

					if (
						renderPass != RENDERPASS_PREPASS &&
						renderPass != RENDERPASS_PREPASS_DEPTHONLY &&
						renderPass != RENDERPASS_VOXELIZE
						)
					{
						// depth only alpha test will be full res
						device->BindShadingRate(material.shadingRate, cmd);
					}

					MeshPushConstants push;
					push.geometryIndex = geometry.geometryOffset + part_index;
					push.materialIndex = material_index;
					push.instBufferResIndex = renderable.resLookupIndex + part_index;
					push.instances = instanceBufferDescriptorIndex;
					push.instance_offset = (uint)instancedBatch.dataOffset;

					if (pso_backside != nullptr)
					{
						device->BindPipelineState(pso_backside, cmd);
						device->PushConstants(&push, sizeof(push), cmd);
						if (is_meshshader_pso)
						{
							assert(0 && "Not Yet Supported!");
							//device->DispatchMesh((geometry.cluster_ranges[part_index].clusterCount + 31) / 32, instancedBatch.instanceCount, 1, cmd);
						}
						else
						{
							//device->DrawIndexedInstanced(part.indexCount, instancedBatch.instanceCount, part.indexOffset, 0, 0, cmd);
							device->DrawIndexedInstanced(part.GetNumIndices(), instancedBatch.instanceCount, 0, 0, 0, cmd);
						}
					}

					device->BindPipelineState(pso, cmd);
					device->PushConstants(&push, sizeof(push), cmd);
					if (is_meshshader_pso)
					{
						assert(0 && "Not Yet Supported!");
						//device->DispatchMesh((geometry.cluster_ranges[part_index].clusterCount + 31) / 32, instancedBatch.instanceCount, 1, cmd);
					}
					else
					{
						//device->DrawIndexedInstanced(part.indexCount, instancedBatch.instanceCount, part.indexOffset, 0, 0, cmd);
						device->DrawIndexedInstanced(part.GetNumIndices(), instancedBatch.instanceCount, 0, 0, 0, cmd);
					}

				}
			};

		// The following loop is writing the instancing batches to a GPUBuffer:
		//	RenderQueue is sorted based on mesh index, so when a new mesh or stencil request is encountered, we need to flush the batch
		//	Imagine a scenario:
		//		* tens of sphere-shaped renderables (actors) that have the same sphere geoemtry
		//		* multiple draw calls of the renderables vs. a single drawing of multiple instances (composed of spheres)
		uint32_t instanceCount = 0;
		for (const RenderBatch& batch : renderQueue.batches) // Do not break out of this loop!
		{
			const uint32_t geometry_index = batch.GetGeometryIndex();	// geometry index
			const uint32_t renderable_index = batch.GetRenderableIndex();	// renderable index (base renderable)
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderable_index];
			assert(renderable.IsMeshRenderable());

			// TODO.. 
			//	to implement multi-instancing
			//	here, apply instance meta information
			//		e.g., AABB, transforms, colors, ...
			const AABB& instanceAABB = renderable.GetAABB();

			// When we encounter a new mesh inside the global instance array, we begin a new RenderBatch:
			if (geometry_index != instancedBatch.geometryIndex ||
				renderable.lod != instancedBatch.lod
				)
			{
				BatchDrawingFlush();

				instancedBatch = {};
				instancedBatch.geometryIndex = geometry_index;
				instancedBatch.renderableIndex = renderable_index;
				instancedBatch.instanceCount = 0;	// rendering camera count..
				instancedBatch.dataOffset = (uint32_t)(instances.offset + instanceCount * sizeof(ShaderMeshInstancePointer));
				instancedBatch.forceAlphatestForDithering = 0;
				instancedBatch.aabb = AABB();
				instancedBatch.lod = renderable.lod;
				std::vector<Entity> materials(renderable.GetNumParts());
				size_t n = renderable.GetMaterials(materials.data());
				instancedBatch.materialIndices.resize(materials.size());
				for (size_t i = 0; i < n; ++i)
				{
					instancedBatch.materialIndices[i] = Scene::GetIndex(scene_Gdetails->materialEntities, materials[i]);
				}
			}

			const float dither = std::max(0.0f, batch.GetDistance() - renderable.GetFadeDistance()) / renderable.GetVisibleRadius();
			if (dither > 0.99f)
			{
				continue;
			}
			else if (dither > 0)
			{
				instancedBatch.forceAlphatestForDithering = 1;
			}

			if (forward_lightmask_request)
			{
				instancedBatch.aabb = AABB::Merge(instancedBatch.aabb, instanceAABB);
			}

			for (uint32_t camera_index = 0; camera_index < camera_count; ++camera_index)
			{
				const uint16_t camera_mask = 1 << camera_index;
				if ((batch.camera_mask & camera_mask) == 0)
					continue;

				ShaderMeshInstancePointer poi;
				poi.Create(renderable_index, camera_index, dither);

				// Write into actual GPU-buffer:
				std::memcpy((ShaderMeshInstancePointer*)instances.data + instanceCount, &poi, sizeof(poi)); // memcpy whole structure into mapped pointer to avoid read from uncached memory

				instancedBatch.instanceCount++; // next instance in current InstancedBatch
				instanceCount++;
			}
		}

		BatchDrawingFlush();

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::RenderPostprocessChain(CommandList cmd)
	{
		BindCommonResources(cmd);
		BindCameraCB(*camera, cameraPrevious, cameraReflection, cmd);

		const Texture* rt_first = nullptr; // not ping-ponged with read / write
		const Texture* rt_read = &rtMain;
		const Texture* rt_write = &rtPostprocess;

		// rtPostprocess aliasing transition:
		{
			GPUBarrier barriers[] = {
				GPUBarrier::Aliasing(&rtPrimitiveID_1, &rtPostprocess),
				GPUBarrier::Image(&rtPostprocess, rtPostprocess.desc.layout, ResourceState::UNORDERED_ACCESS),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
			device->ClearUAV(&rtPostprocess, 0, cmd);
			device->Barrier(GPUBarrier::Image(&rtPostprocess, ResourceState::UNORDERED_ACCESS, rtPostprocess.desc.layout), cmd);
		}

		// 1.) HDR post process chain
		{
			// TODO: TAA, FSR, DOF, MBlur
			/*
			if (getFSR2Enabled() && fsr2Resources.IsValid())
			{
				renderer::Postprocess_FSR2(
					fsr2Resources,
					*camera,
					rtFSR[1],
					*rt_read,
					depthBuffer_Copy,
					rtVelocity,
					rtFSR[0],
					cmd,
					scene->dt,
					getFSR2Sharpness()
				);

				// rebind these, because FSR2 binds other things to those constant buffers:
				renderer::BindCameraCB(
					*camera,
					camera_previous,
					camera_reflection,
					cmd
				);
				renderer::BindCommonResources(cmd);

				rt_read = &rtFSR[0];
				rt_write = &rtFSR[1];
			}
			else if (renderer::GetTemporalAAEnabled() && !renderer::GetTemporalAADebugEnabled() && temporalAAResources.IsValid())
			{
				renderer::Postprocess_TemporalAA(
					temporalAAResources,
					*rt_read,
					cmd
				);
				rt_first = temporalAAResources.GetCurrent();
			}

			if (getDepthOfFieldEnabled() && camera->aperture_size > 0.001f && getDepthOfFieldStrength() > 0.001f && depthoffieldResources.IsValid())
			{
				renderer::Postprocess_DepthOfField(
					depthoffieldResources,
					rt_first == nullptr ? *rt_read : *rt_first,
					*rt_write,
					cmd,
					getDepthOfFieldStrength()
				);

				rt_first = nullptr;
				std::swap(rt_read, rt_write);
			}

			if (getMotionBlurEnabled() && getMotionBlurStrength() > 0 && motionblurResources.IsValid())
			{
				renderer::Postprocess_MotionBlur(
					motionblurResources,
					rt_first == nullptr ? *rt_read : *rt_first,
					*rt_write,
					cmd,
					getMotionBlurStrength()
				);

				rt_first = nullptr;
				std::swap(rt_read, rt_write);
			}
			/**/
		}

		// 2.) Tone mapping HDR -> LDR
		{
			// Bloom and eye adaption is not part of post process "chain",
			//	because they will be applied to the screen in tonemap
			
			//if (getEyeAdaptionEnabled())
			//{
			//	renderer::ComputeLuminance(
			//		luminanceResources,
			//		rt_first == nullptr ? *rt_read : *rt_first,
			//		cmd,
			//		getEyeAdaptionRate(),
			//		getEyeAdaptionKey()
			//	);
			//}
			//if (getBloomEnabled())
			//{
			//	renderer::ComputeBloom(
			//		bloomResources,
			//		rt_first == nullptr ? *rt_read : *rt_first,
			//		cmd,
			//		getBloomThreshold(),
			//		getExposure(),
			//		getEyeAdaptionEnabled() ? &luminanceResources.luminance : nullptr
			//	);
			//}
			
			Postprocess_Tonemap(
				rt_first == nullptr ? *rt_read : *rt_first,
				*rt_write,
				cmd,
				camera->GetSensorExposure(),
				camera->GetSensorBrightness(),
				camera->GetSensorContrast(),
				camera->GetSensorSaturation(),
				false, //getDitherEnabled(),
				isColorGradingEnabled ? (const graphics::Texture*)scene->GetTextureGradientMap() : nullptr,
				&rtParticleDistortion,
				camera->IsSensorEyeAdaptationEnabled() ? &luminanceResources.luminance : nullptr,
				camera->IsSensorBloomEnabled() ? &bloomResources.texture_bloom : nullptr,
				colorspace,
				tonemap,
				&distortion_overlay,
				camera->GetSensorHdrCalibration()
			);

			rt_first = nullptr;
			std::swap(rt_read, rt_write);
		}

		// 3.) LDR post process chain
		{
			/*
			if (getSharpenFilterEnabled())
			{
				renderer::Postprocess_Sharpen(*rt_read, *rt_write, cmd, getSharpenFilterAmount());

				std::swap(rt_read, rt_write);
			}

			if (getFXAAEnabled())
			{
				renderer::Postprocess_FXAA(*rt_read, *rt_write, cmd);

				std::swap(rt_read, rt_write);
			}

			if (getChromaticAberrationEnabled())
			{
				renderer::Postprocess_Chromatic_Aberration(*rt_read, *rt_write, cmd, getChromaticAberrationAmount());

				std::swap(rt_read, rt_write);
			}
			/**/

			lastPostprocessRT = rt_read;

			// GUI Background blurring:
			//{
			//	auto range = profiler::BeginRangeGPU("GUI Background Blur", cmd);
			//	device->EventBegin("GUI Background Blur", cmd);
			//	renderer::Postprocess_Downsample4x(*rt_read, rtGUIBlurredBackground[0], cmd);
			//	renderer::Postprocess_Downsample4x(rtGUIBlurredBackground[0], rtGUIBlurredBackground[2], cmd);
			//	renderer::Postprocess_Blur_Gaussian(rtGUIBlurredBackground[2], rtGUIBlurredBackground[1], rtGUIBlurredBackground[2], cmd, -1, -1, true);
			//	device->EventEnd(cmd);
			//	profiler::EndRange(range);
			//}
			//
			//if (rtFSR[0].IsValid() && getFSREnabled())
			//{
			//	renderer::Postprocess_FSR(*rt_read, rtFSR[1], rtFSR[0], cmd, getFSRSharpness());
			//	lastPostprocessRT = &rtFSR[0];
			//}
		}
	}
}

namespace vz
{
	// ---------- GRenderPath3D's interfaces: -----------------

	bool GRenderPath3DDetails::ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight)
	{
		if (canvasWidth_ == canvasWidth && canvasHeight_ == canvasHeight)
		{
			return true;
		}
		firstFrame = true;

		canvasWidth_ = canvasWidth;
		canvasHeight_ = canvasHeight;
		XMUINT2 internalResolution(canvasWidth, canvasHeight);

		Destroy();
		
		// resources associated with render target buffers and textures

		// ----- Render targets:-----

		{ // rtMain, rtMain_render
			TextureDesc desc;
			desc.format = FORMAT_rendertargetMain;
			desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			desc.sample_count = 1;
			device->CreateTexture(&desc, nullptr, &rtMain);
			device->SetName(&rtMain, "rtMain");

			if (msaaSampleCount > 1)
			{
				desc.sample_count = msaaSampleCount;
				desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;

				device->CreateTexture(&desc, nullptr, &rtMain_render);
				device->SetName(&rtMain_render, "rtMain_render");
			}
			else
			{
				rtMain_render = rtMain;
			}
		}
		{ // rtPrimitiveID, rtPrimitiveID_render
			TextureDesc desc;
			desc.format = FORMAT_idbuffer;
			desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
			if (msaaSampleCount > 1)
			{
				desc.bind_flags |= BindFlag::UNORDERED_ACCESS;
			}
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			desc.sample_count = 1;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			desc.misc_flags = ResourceMiscFlag::ALIASING_TEXTURE_RT_DS;
			device->CreateTexture(&desc, nullptr, &rtPrimitiveID_1);
			device->CreateTexture(&desc, nullptr, &rtPrimitiveID_2);
			if (debugMode == DEBUG_BUFFER::PRIMITIVE_ID)
			{
				desc.misc_flags = ResourceMiscFlag::NONE;
				device->CreateTexture(&desc, nullptr, &rtPrimitiveID_debug);
				device->SetName(&rtPrimitiveID_debug, "rtPrimitiveID_debug");
			}
			device->SetName(&rtPrimitiveID_1, "rtPrimitiveID_1");
			device->SetName(&rtPrimitiveID_2, "rtPrimitiveID_2");

			if (msaaSampleCount > 1)
			{
				desc.sample_count = msaaSampleCount;
				desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
				desc.misc_flags = ResourceMiscFlag::NONE;
				device->CreateTexture(&desc, nullptr, &rtPrimitiveID_1_render);
				device->CreateTexture(&desc, nullptr, &rtPrimitiveID_2_render);
				device->SetName(&rtPrimitiveID_1_render, "rtPrimitiveID_1_render");
			}
			else
			{
				rtPrimitiveID_1_render = rtPrimitiveID_1;
				rtPrimitiveID_2_render = rtPrimitiveID_2;
			}
		}
		{ // rtPostprocess
			TextureDesc desc;
			desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.format = FORMAT_rendertargetMain;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			// the same size of format is recommended. the following condition (less equal) will cause some unexpected behavior.
			assert(ComputeTextureMemorySizeInBytes(desc) <= ComputeTextureMemorySizeInBytes(rtPrimitiveID_1.desc)); // Aliased check
			device->CreateTexture(&desc, nullptr, &rtPostprocess, &rtPrimitiveID_1); // Aliased!
			device->SetName(&rtPostprocess, "rtPostprocess");
		}
		if (device->CheckCapability(GraphicsDeviceCapability::VARIABLE_RATE_SHADING_TIER2) &&
			isVariableRateShadingClassification)
		{ // rtShadingRate
			uint32_t tileSize = device->GetVariableRateShadingTileSize();

			TextureDesc desc;
			desc.layout = ResourceState::UNORDERED_ACCESS;
			desc.bind_flags = BindFlag::UNORDERED_ACCESS | BindFlag::SHADING_RATE;
			desc.format = Format::R8_UINT;
			desc.width = (internalResolution.x + tileSize - 1) / tileSize;
			desc.height = (internalResolution.y + tileSize - 1) / tileSize;

			device->CreateTexture(&desc, nullptr, &rtShadingRate);
			device->SetName(&rtShadingRate, "rtShadingRate");
		}

		//----- Depth buffers: -----

		{ // depthBufferMain, depthBuffer_Copy, depthBuffer_Copy1
			TextureDesc desc;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			
			desc.sample_count = msaaSampleCount;
			desc.layout = ResourceState::DEPTHSTENCIL;
			desc.format = FORMAT_depthbufferMain;
			desc.bind_flags = BindFlag::DEPTH_STENCIL;
			device->CreateTexture(&desc, nullptr, &depthBufferMain);
			device->SetName(&depthBufferMain, "depthBufferMain");

			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			desc.format = Format::R32_FLOAT;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.sample_count = 1;
			desc.mip_levels = std::min(5u, (uint32_t)std::log2(std::max(desc.width, desc.height))); //5;
			device->CreateTexture(&desc, nullptr, &depthBuffer_Copy);
			device->SetName(&depthBuffer_Copy, "depthBuffer_Copy");
			device->CreateTexture(&desc, nullptr, &depthBuffer_Copy1);
			device->SetName(&depthBuffer_Copy1, "depthBuffer_Copy1");

			for (uint32_t i = 0; i < depthBuffer_Copy.desc.mip_levels; ++i)
			{
				int subresource = 0;
				subresource = device->CreateSubresource(&depthBuffer_Copy, SubresourceType::SRV, 0, 1, i, 1);
				assert(subresource == i);
				subresource = device->CreateSubresource(&depthBuffer_Copy, SubresourceType::UAV, 0, 1, i, 1);
				assert(subresource == i);
				subresource = device->CreateSubresource(&depthBuffer_Copy1, SubresourceType::SRV, 0, 1, i, 1);
				assert(subresource == i);
				subresource = device->CreateSubresource(&depthBuffer_Copy1, SubresourceType::UAV, 0, 1, i, 1);
				assert(subresource == i);
			}
		}
		{ // rtLinearDepth
			TextureDesc desc;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.format = Format::R32_FLOAT;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			//desc.mip_levels = 5;
			desc.mip_levels = std::min(5u, (uint32_t)std::log2(std::max(desc.width, desc.height)));
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			device->CreateTexture(&desc, nullptr, &rtLinearDepth);
			device->SetName(&rtLinearDepth, "rtLinearDepth");

			for (uint32_t i = 0; i < desc.mip_levels; ++i)
			{
				int subresource_index;
				subresource_index = device->CreateSubresource(&rtLinearDepth, SubresourceType::SRV, 0, 1, i, 1);
				assert(subresource_index == i);
				subresource_index = device->CreateSubresource(&rtLinearDepth, SubresourceType::UAV, 0, 1, i, 1);
				assert(subresource_index == i);
			}
		}

		//----- Other resources: -----
		{ // debugUAV
			TextureDesc desc;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			desc.mip_levels = 1;
			desc.array_size = 1;
			desc.format = Format::R8G8B8A8_UNORM;
			desc.sample_count = 1;
			desc.usage = Usage::DEFAULT;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.layout = ResourceState::SHADER_RESOURCE;

			device->CreateTexture(&desc, nullptr, &debugUAV);
			device->SetName(&debugUAV, "debugUAV");
		}
		CreateTiledLightResources(tiledLightResources, internalResolution);
		//CreateScreenSpaceShadowResources(screenspaceshadowResources, internalResolution);

		return true;
	}

	void GRenderPath3DDetails::Compose(CommandList cmd) 
	{
		auto range = profiler::BeginRangeCPU("Compose");

		image::SetCanvas(canvasWidth_, canvasHeight_, 96.F);

		image::Params fx;
		fx.blendFlag = MaterialComponent::BlendMode::BLENDMODE_OPAQUE;
		fx.quality = image::QUALITY_LINEAR;
		fx.enableFullScreen();

		device->EventBegin("Composition", cmd);

		if (debugMode != DEBUG_BUFFER::NONE)
		{
			XMUINT2 internalResolution(canvasWidth_, canvasHeight_);
			fx.enableDebugTest();
			fx.setDebugBuffer(static_cast<image::Params::DEBUG_BUFFER>(debugMode));
			switch (debugMode)
			{
			case DEBUG_BUFFER::PRIMITIVE_ID:
			case DEBUG_BUFFER::INSTANCE_ID:
				if (!rtPrimitiveID_debug.IsValid())
				{
					TextureDesc desc;
					desc.format = FORMAT_idbuffer;
					desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
					if (msaaSampleCount > 1)
					{
						desc.bind_flags |= BindFlag::UNORDERED_ACCESS;
					}
					desc.width = internalResolution.x;
					desc.height = internalResolution.y;
					desc.sample_count = 1;
					desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
					desc.misc_flags = ResourceMiscFlag::ALIASING_TEXTURE_RT_DS;
					device->CreateTexture(&desc, nullptr, &rtPrimitiveID_debug);
					device->SetName(&rtPrimitiveID_debug, "rtPrimitiveID_debug");
				}
				image::Draw(&rtPrimitiveID_debug, fx, cmd);
				break;
			case DEBUG_BUFFER::LINEAR_DEPTH:
				image::Draw(&rtLinearDepth, fx, cmd);
				break;
			case DEBUG_BUFFER::WITHOUT_POSTPROCESSING:
				image::Draw(&rtMain, fx, cmd);
				break;
			case DEBUG_BUFFER::NONE:
			default:
				assert(0);
				break;
			}
		}
		else
		{
			rtPrimitiveID_debug = {};
			image::Draw(lastPostprocessRT, fx, cmd);
		}
		device->EventEnd(cmd);

		if (
			isDebugLightCulling || 
			isVariableRateShadingClassification || 
			isSurfelGIDebugEnabled
			)
		{
			fx.enableFullScreen();
			fx.blendFlag = MaterialComponent::BlendMode::BLENDMODE_PREMULTIPLIED;
			image::Draw(&debugUAV, fx, cmd);
		}

		profiler::EndRange(range);
	}

	bool GRenderPath3DDetails::Render(const float dt)
	{
		if (!initializer::IsInitialized())
		{
			return false;
		}

		if (((GCameraComponent*)camera)->isPickingMode)
		{
			// TODO
			return true;
		}

		profiler::BeginFrame();
		// color space check
		// if swapChain is invalid, rtRenderFinal_ is supposed to be valid!
		if (swapChain_.IsValid())
		{
			ColorSpace colorspace = device->GetSwapChainColorSpace(&swapChain_);
			if (colorspace == ColorSpace::HDR10_ST2084)
			{
				if (!rtRenderFinal_.IsValid())
				{
					TextureDesc desc;
					desc.width = swapChain_.desc.width;
					desc.height = swapChain_.desc.height;
					desc.format = Format::R11G11B10_FLOAT;
					desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
					bool success = device->CreateTexture(&desc, nullptr, &rtRenderFinal_);
					assert(success);
					device->SetName(&rtRenderFinal_, "GRenderPath3DDetails::rtRenderFinal_");
				}
			}
			else
			{
				rtRenderFinal_ = {};
			}
		}

		// ----- main render process -----
		auto range = profiler::BeginRangeCPU("Render");

		UpdateProcess(dt);
		RenderProcess();
		profiler::EndRange(range);

		graphics::CommandList cmd = device->BeginCommandList();
		// Begin final compositing:
		graphics::Viewport viewport_composite; // full buffer
		viewport_composite.width = (float)canvasWidth_;
		viewport_composite.height = (float)canvasHeight_;
		device->BindViewports(1, &viewport, cmd);

		if (rtRenderFinal_.IsValid())
		{
			graphics::RenderPassImage rp[] = {
				graphics::RenderPassImage::RenderTarget(&rtRenderFinal_, graphics::RenderPassImage::LoadOp::CLEAR),
			};
			device->RenderPassBegin(rp, arraysize(rp), cmd);
		}
		else
		{
			device->RenderPassBegin(&swapChain_, cmd);
		}

		Compose(cmd);
		device->RenderPassEnd(cmd);

		if (rtRenderFinal_.IsValid() && swapChain_.IsValid())
		{
			// colorspace == ColorSpace::HDR10_ST2084
			// In HDR10, we perform a final mapping from linear to HDR10, into the swapchain
			device->RenderPassBegin(&swapChain_, cmd);
			image::Params fx;
			fx.enableFullScreen();
			fx.enableHDR10OutputMapping();
			image::Draw(&rtRenderFinal_, fx, cmd);	// note: in this case, rtRenderFinal_ is used as inter-result of final render-out
			device->RenderPassEnd(cmd);
		}

		profiler::EndFrame(&cmd); // cmd must be assigned before SubmitCommandLists

		device->SubmitCommandLists();

		return true;
	}

	bool GRenderPath3DDetails::RenderProcess()
	{
		jobsystem::context ctx;

		// Preparing the frame:
		CommandList cmd = device->BeginCommandList();
		device->WaitQueue(cmd, QUEUE_COMPUTE); // sync to prev frame compute (disallow prev frame overlapping a compute task into updating global scene resources for this frame)
		ProcessDeferredTextureRequests(cmd); // Execute it first thing in the frame here, on main thread, to not allow other thread steal it and execute on different command list!
		
		CommandList cmd_prepareframe = cmd;
		// remember GraphicsDevice::BeginCommandList does incur some overhead
		//	this is why jobsystem::Execute is used here
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(*camera, cameraPrevious, cameraReflection, cmd);
			UpdateRenderData(viewMain, frameCB, cmd);

			uint32_t num_barriers = 2;
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&debugUAV, debugUAV.desc.layout, ResourceState::UNORDERED_ACCESS),
				GPUBarrier::Aliasing(&rtPostprocess, &rtPrimitiveID_1),
				GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::SHADER_RESOURCE_COMPUTE), // prepares transition for discard in dx12
			};
			if (viewShadingInCS)
			{
				num_barriers++;
			}
			device->Barrier(barriers, num_barriers, cmd);

			});

		// async compute parallel with depth prepass
		cmd = device->BeginCommandList(QUEUE_COMPUTE);
		CommandList cmd_prepareframe_async = cmd;
		device->WaitCommandList(cmd, cmd_prepareframe);

		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(*camera, cameraPrevious, cameraReflection, cmd);
			UpdateRenderDataAsync(viewMain, frameCB, cmd);

			// UpdateRaytracingAccelerationStructures
			// ComputeSkyAtmosphere
			// SurfelGI
			// DDGI

			});

		static const uint32_t drawscene_regular_flags = 
			renderer::DRAWSCENE_OPAQUE |
			renderer::DRAWSCENE_TESSELLATION |
			renderer::DRAWSCENE_OCCLUSIONCULLING;

		// Main camera depth prepass:
		cmd = device->BeginCommandList();
		CommandList cmd_maincamera_prepass = cmd;
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(*camera, cameraPrevious, cameraReflection, cmd);

			// Mesh Shader
			//if (reprojectedDepth.IsValid())
			//{
			//	renderer::ComputeReprojectedDepthPyramid(
			//		depthBuffer_Copy,
			//		rtVelocity,
			//		reprojectedDepth,
			//		cmd
			//	);
			//}

			RenderPassImage rp[] = {
				RenderPassImage::DepthStencil(
					&depthBufferMain,
					RenderPassImage::LoadOp::CLEAR,
					RenderPassImage::StoreOp::STORE,
					ResourceState::DEPTHSTENCIL,
					ResourceState::DEPTHSTENCIL,
					ResourceState::DEPTHSTENCIL
				),
				RenderPassImage::RenderTarget(
					&rtPrimitiveID_1_render,
					RenderPassImage::LoadOp::CLEAR,
					RenderPassImage::StoreOp::STORE,
					ResourceState::SHADER_RESOURCE_COMPUTE,
					ResourceState::SHADER_RESOURCE_COMPUTE
				),
				RenderPassImage::RenderTarget(
					&rtPrimitiveID_2_render,
					RenderPassImage::LoadOp::CLEAR,
					RenderPassImage::StoreOp::STORE,
					ResourceState::SHADER_RESOURCE_COMPUTE,
					ResourceState::SHADER_RESOURCE_COMPUTE
				),
			};
			device->RenderPassBegin(rp, arraysize(rp), cmd);

			device->EventBegin("Opaque Z-prepass", cmd);
			auto range = profiler::BeginRangeGPU("Z-Prepass", (CommandList*)&cmd);

			device->BindScissorRects(1, &scissor, cmd);

			Viewport vp;// = downcast_camera->viewport; // TODO.. viewport just for render-out result vs. viewport for enhancing performance...?!
			vp.width = (float)depthBufferMain.GetDesc().width;
			vp.height = (float)depthBufferMain.GetDesc().height;

			// ----- Foreground: -----
			vp.min_depth = 1.f - foregroundDepthRange;
			vp.max_depth = 1.f;
			device->BindViewports(1, &vp, cmd);
			DrawScene(
				viewMain,
				RENDERPASS_PREPASS,
				cmd,
				renderer::DRAWSCENE_OPAQUE |
				renderer::DRAWSCENE_FOREGROUND_ONLY
			);

			// ----- Regular: -----
			vp.min_depth = 0;
			vp.max_depth = 1;
			device->BindViewports(1, &vp, cmd);
			DrawScene(
				viewMain,
				RENDERPASS_PREPASS,
				cmd,
				drawscene_regular_flags
			);

			profiler::EndRange(range);
			device->EventEnd(cmd);

			device->RenderPassEnd(cmd);

			if ((debugMode == DEBUG_BUFFER::PRIMITIVE_ID || debugMode == DEBUG_BUFFER::INSTANCE_ID)
				&& rtPrimitiveID_debug.IsValid())
			{
				graphics::Texture& rtSrc = debugMode == DEBUG_BUFFER::PRIMITIVE_ID ? rtPrimitiveID_1_render : rtPrimitiveID_2_render;
				GPUBarrier barriers1[] = {
					GPUBarrier::Image(&rtSrc, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::COPY_SRC),
					GPUBarrier::Image(&rtPrimitiveID_debug, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::COPY_DST),
				};
				device->Barrier(barriers1, arraysize(barriers1), cmd);
				device->CopyTexture(
					&rtPrimitiveID_debug, 0, 0, 0, 0, 0,
					&rtSrc, 0, 0,
					cmd
				);
				GPUBarrier barriers2[] = {
					GPUBarrier::Image(&rtSrc, ResourceState::COPY_SRC, ResourceState::SHADER_RESOURCE_COMPUTE),
					GPUBarrier::Image(&rtPrimitiveID_debug, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE_COMPUTE),
				};
				device->Barrier(barriers2, arraysize(barriers2), cmd);
			}

			});

		// Main camera compute effects:
		//	(async compute, parallel to "shadow maps" and "update textures",
		//	must finish before "main scene opaque color pass")
		cmd = device->BeginCommandList(QUEUE_COMPUTE);
		device->WaitCommandList(cmd, cmd_maincamera_prepass);

		CommandList cmd_maincamera_compute_effects = cmd;
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(
				*camera,
				cameraPrevious,
				cameraReflection,
				cmd
			);

			View_Prepare(
				viewResources,
				rtPrimitiveID_1_render,
				rtPrimitiveID_2_render,
				cmd
			);

			ComputeTiledLightCulling(
				tiledLightResources,
				viewMain,
				debugUAV,
				cmd
			);

			if (viewShadingInCS)
			{
				View_Surface(
					viewResources,
					rtMain,
					cmd
				);
			}
			//else if (
			//	getSSREnabled() ||
			//	getSSGIEnabled() ||
			//	getRaytracedReflectionEnabled() ||
			//	getRaytracedDiffuseEnabled() ||
			//	renderer::GetScreenSpaceShadowsEnabled() ||
			//	renderer::GetRaytracedShadowsEnabled() ||
			//	renderer::GetVXGIEnabled()
			//	)
			//{
			//	// These post effects require surface normals and/or roughness
			//	renderer::Visibility_Surface_Reduced(
			//		visibilityResources,
			//		cmd
			//	);
			//}

			//if (renderer::GetSurfelGIEnabled())
			//{
			//	renderer::SurfelGI_Coverage(
			//		surfelGIResources,
			//		*scene,
			//		rtLinearDepth,
			//		debugUAV,
			//		cmd
			//	);
			//}
			
			//RenderAO(cmd);

			//if (renderer::GetVariableRateShadingClassification() && device->CheckCapability(GraphicsDeviceCapability::VARIABLE_RATE_SHADING_TIER2))
			//{
			//	renderer::ComputeShadingRateClassification(
			//		rtShadingRate,
			//		debugUAV,
			//		cmd
			//	);
			//}
			
			//RenderSSR(cmd);
			
			//RenderSSGI(cmd);

			//if (renderer::GetScreenSpaceShadowsEnabled())
			//{
			//	renderer::Postprocess_ScreenSpaceShadow(
			//		screenspaceshadowResources,
			//		tiledLightResources.entityTiles,
			//		rtLinearDepth,
			//		rtShadow,
			//		cmd,
			//		getScreenSpaceShadowRange(),
			//		getScreenSpaceShadowSampleCount()
			//	);
			//}

			//if (renderer::GetRaytracedShadowsEnabled())
			//{
			//	renderer::Postprocess_RTShadow(
			//		rtshadowResources,
			//		*scene,
			//		tiledLightResources.entityTiles,
			//		rtLinearDepth,
			//		rtShadow,
			//		cmd
			//	);
			//}

			});
		
		// Occlusion culling:
		CommandList cmd_occlusionculling;
		if (renderer::isOcclusionCullingEnabled)
		{
			cmd = device->BeginCommandList();
			cmd_occlusionculling = cmd;
			jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

				device->EventBegin("Occlusion Culling", cmd);
				ScopedGPUProfiling("Occlusion Culling", (CommandList*)&cmd);
				
				BindCameraCB(
					*camera,
					cameraPrevious,
					cameraReflection,
					cmd
				);
				
				OcclusionCulling_Reset(viewMain, cmd); // must be outside renderpass!
								
				RenderPassImage rp[] = {
					RenderPassImage::DepthStencil(&depthBufferMain),
				};
				device->RenderPassBegin(rp, arraysize(rp), cmd);
				
				device->BindScissorRects(1, &scissor, cmd);
				
				Viewport vp;
				vp.width = (float)depthBufferMain.GetDesc().width;
				vp.height = (float)depthBufferMain.GetDesc().height;
				device->BindViewports(1, &vp, cmd);
				
				OcclusionCulling_Render(*camera, viewMain, cmd);
				
				device->RenderPassEnd(cmd);

				OcclusionCulling_Resolve(viewMain, cmd); // must be outside renderpass!

				device->EventEnd(cmd);
				});
		}
		
		// Shadow maps:
		if (isShadowsEnabled)
		{
			//cmd = device->BeginCommandList();
			//jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {
			//	renderer::DrawShadowmaps(visibility_main, cmd);
			//	});
		}

		//if (renderer::GetVXGIEnabled() && getSceneUpdateEnabled())
		//{
		//	cmd = device->BeginCommandList();
		//	jobsystem::Execute(ctx, [cmd, this](jobsystem::JobArgs args) {
		//		renderer::VXGI_Voxelize(visibility_main, cmd);
		//		});
		//}

		// Updating textures:
		//if (getSceneUpdateEnabled())
		//{
		//	cmd = device->BeginCommandList();
		//	device->WaitCommandList(cmd, cmd_prepareframe_async);
		//	jobsystem::Execute(ctx, [cmd, this](jobsystem::JobArgs args) {
		//		BindCommonResources(cmd);
		//		BindCameraCB(
		//			*camera,
		//			cameraPrevious,
		//			cameraReflection,
		//			cmd
		//		);
		//		renderer::RefreshLightmaps(*scene, cmd);
		//		renderer::RefreshEnvProbes(visibility_main, cmd);
		//		});
		//}

		//if (getReflectionsEnabled() && visibility_main.IsRequestedPlanarReflections())
		//{
		//	// Planar reflections depth prepass:
		//	cmd = device->BeginCommandList();
		//	jobsystem::Execute(ctx, [cmd, this](jobsystem::JobArgs args) {
		//
		//		GraphicsDevice* device = graphics::GetDevice();
		//
		//		renderer::BindCameraCB(
		//			camera_reflection,
		//			camera_reflection_previous,
		//			camera_reflection,
		//			cmd
		//		);
		//
		//		device->EventBegin("Planar reflections Z-Prepass", cmd);
		//		auto range = profiler::BeginRangeGPU("Planar Reflections Z-Prepass", cmd);
		//
		//		RenderPassImage rp[] = {
		//			RenderPassImage::DepthStencil(
		//				&depthBuffer_Reflection,
		//				RenderPassImage::LoadOp::CLEAR,
		//				RenderPassImage::StoreOp::STORE,
		//				ResourceState::DEPTHSTENCIL,
		//				ResourceState::DEPTHSTENCIL,
		//				ResourceState::SHADER_RESOURCE
		//			),
		//		};
		//		device->RenderPassBegin(rp, arraysize(rp), cmd);
		//
		//		Viewport vp;
		//		vp.width = (float)depthBuffer_Reflection.GetDesc().width;
		//		vp.height = (float)depthBuffer_Reflection.GetDesc().height;
		//		vp.min_depth = 0;
		//		vp.max_depth = 1;
		//		device->BindViewports(1, &vp, cmd);
		//
		//		renderer::DrawScene(
		//			visibility_reflection,
		//			RENDERPASS_PREPASS_DEPTHONLY,
		//			cmd,
		//			renderer::DRAWSCENE_OPAQUE |
		//			renderer::DRAWSCENE_IMPOSTOR |
		//			renderer::DRAWSCENE_HAIRPARTICLE |
		//			renderer::DRAWSCENE_SKIP_PLANAR_REFLECTION_OBJECTS
		//		);
		//
		//		device->RenderPassEnd(cmd);
		//
		//		profiler::EndRange(range); // Planar Reflections
		//		device->EventEnd(cmd);
		//
		//		});
		//
		//	// Planar reflections color pass:
		//	cmd = device->BeginCommandList();
		//	jobsystem::Execute(ctx, [cmd, this](jobsystem::JobArgs args) {
		//
		//		GraphicsDevice* device = graphics::GetDevice();
		//
		//		renderer::BindCameraCB(
		//			camera_reflection,
		//			camera_reflection_previous,
		//			camera_reflection,
		//			cmd
		//		);
		//
		//		renderer::ComputeTiledLightCulling(
		//			tiledLightResources_planarReflection,
		//			visibility_reflection,
		//			Texture(),
		//			cmd
		//		);
		//
		//		device->EventBegin("Planar reflections", cmd);
		//		auto range = profiler::BeginRangeGPU("Planar Reflections", cmd);
		//
		//		RenderPassImage rp[] = {
		//			RenderPassImage::RenderTarget(
		//				&rtReflection,
		//				RenderPassImage::LoadOp::DONTCARE,
		//				RenderPassImage::StoreOp::STORE,
		//				ResourceState::SHADER_RESOURCE,
		//				ResourceState::SHADER_RESOURCE
		//			),
		//			RenderPassImage::DepthStencil(
		//				&depthBuffer_Reflection,
		//				RenderPassImage::LoadOp::LOAD,
		//				RenderPassImage::StoreOp::STORE,
		//				ResourceState::SHADER_RESOURCE
		//			),
		//		};
		//		device->RenderPassBegin(rp, arraysize(rp), cmd);
		//
		//		Viewport vp;
		//		vp.width = (float)depthBuffer_Reflection.GetDesc().width;
		//		vp.height = (float)depthBuffer_Reflection.GetDesc().height;
		//		vp.min_depth = 0;
		//		vp.max_depth = 1;
		//		device->BindViewports(1, &vp, cmd);
		//
		//		renderer::DrawScene(
		//			visibility_reflection,
		//			RENDERPASS_MAIN,
		//			cmd,
		//			renderer::DRAWSCENE_OPAQUE |
		//			renderer::DRAWSCENE_IMPOSTOR |
		//			renderer::DRAWSCENE_HAIRPARTICLE |
		//			renderer::DRAWSCENE_SKIP_PLANAR_REFLECTION_OBJECTS
		//		);
		//		renderer::DrawScene(
		//			visibility_reflection,
		//			RENDERPASS_MAIN,
		//			cmd,
		//			renderer::DRAWSCENE_TRANSPARENT |
		//			renderer::DRAWSCENE_SKIP_PLANAR_REFLECTION_OBJECTS
		//		); // separate renderscene, to be drawn after opaque and transparent sort order
		//		renderer::DrawSky(*scene, cmd);
		//
		//		if (scene->weather.IsRealisticSky() && scene->weather.IsRealisticSkyAerialPerspective())
		//		{
		//			// Blend Aerial Perspective on top:
		//			device->EventBegin("Aerial Perspective Reflection Blend", cmd);
		//			image::Params fx;
		//			fx.enableFullScreen();
		//			fx.blendFlag = BLENDMODE_PREMULTIPLIED;
		//			image::Draw(&aerialperspectiveResources_reflection.texture_output, fx, cmd);
		//			device->EventEnd(cmd);
		//		}
		//
		//		// Blend the volumetric clouds on top:
		//		//	For planar reflections, we don't use upsample, because there is no linear depth here
		//		if (scene->weather.IsVolumetricClouds())
		//		{
		//			device->EventBegin("Volumetric Clouds Reflection Blend", cmd);
		//			image::Params fx;
		//			fx.enableFullScreen();
		//			fx.blendFlag = BLENDMODE_PREMULTIPLIED;
		//			image::Draw(&volumetriccloudResources_reflection.texture_reproject[volumetriccloudResources_reflection.frame % 2], fx, cmd);
		//			device->EventEnd(cmd);
		//		}
		//
		//		renderer::DrawSoftParticles(visibility_reflection, false, cmd);
		//		renderer::DrawSpritesAndFonts(*scene, camera_reflection, false, cmd);
		//
		//		device->RenderPassEnd(cmd);
		//
		//		profiler::EndRange(range); // Planar Reflections
		//		device->EventEnd(cmd);
		//		});
		//}

		// Main camera opaque color pass:
		cmd = device->BeginCommandList();
		device->WaitCommandList(cmd, cmd_maincamera_compute_effects);
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			device->EventBegin("Opaque Scene", cmd);

			BindCameraCB(
				*camera,
				cameraPrevious,
				cameraReflection,
				cmd
			);

			//if (getRaytracedReflectionEnabled())
			//{
			//	renderer::Postprocess_RTReflection(
			//		rtreflectionResources,
			//		*scene,
			//		rtSSR,
			//		cmd,
			//		getRaytracedReflectionsRange(),
			//		getReflectionRoughnessCutoff()
			//	);
			//}
			//if (getRaytracedDiffuseEnabled())
			//{
			//	renderer::Postprocess_RTDiffuse(
			//		rtdiffuseResources,
			//		*scene,
			//		rtRaytracedDiffuse,
			//		cmd,
			//		getRaytracedDiffuseRange()
			//	);
			//}
			//if (renderer::GetVXGIEnabled())
			//{
			//	renderer::VXGI_Resolve(
			//		vxgiResources,
			//		*scene,
			//		rtLinearDepth,
			//		cmd
			//	);
			//}

			// Depth buffers were created on COMPUTE queue, so make them available for pixel shaders here:
			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&rtLinearDepth, rtLinearDepth.desc.layout, ResourceState::SHADER_RESOURCE),
					GPUBarrier::Image(&depthBuffer_Copy, depthBuffer_Copy.desc.layout, ResourceState::SHADER_RESOURCE),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			//if (renderer::GetRaytracedShadowsEnabled() || renderer::GetScreenSpaceShadowsEnabled())
			//{
			//	GPUBarrier barrier = GPUBarrier::Image(&rtShadow, rtShadow.desc.layout, ResourceState::SHADER_RESOURCE);
			//	device->Barrier(&barrier, 1, cmd);
			//}

			if (viewShadingInCS)
			{
				View_Shade(
					viewResources,
					rtMain,
					cmd
				);
			}

			Viewport vp;
			vp.width = (float)depthBufferMain.GetDesc().width;
			vp.height = (float)depthBufferMain.GetDesc().height;
			device->BindViewports(1, &vp, cmd);

			device->BindScissorRects(1, &scissor, cmd);

			//if (getOutlineEnabled())
			//{
			//	// Cut off outline source from linear depth:
			//	device->EventBegin("Outline Source", cmd);
			//
			//	RenderPassImage rp[] = {
			//		RenderPassImage::RenderTarget(&rtOutlineSource, RenderPassImage::LoadOp::CLEAR),
			//		RenderPassImage::DepthStencil(&depthBufferMain, RenderPassImage::LoadOp::LOAD)
			//	};
			//	device->RenderPassBegin(rp, arraysize(rp), cmd);
			//	image::Params params;
			//	params.enableFullScreen();
			//	params.stencilRefMode = image::STENCILREFMODE_ENGINE;
			//	params.stencilComp = image::STENCILMODE_EQUAL;
			//	params.stencilRef = enums::STENCILREF_OUTLINE;
			//	image::Draw(&rtLinearDepth, params, cmd);
			//	params.stencilRef = enums::STENCILREF_CUSTOMSHADER_OUTLINE;
			//	image::Draw(&rtLinearDepth, params, cmd);
			//	device->RenderPassEnd(cmd);
			//	device->EventEnd(cmd);
			//}

			RenderPassImage rp[4] = {};
			uint32_t rp_count = 0;
			rp[rp_count++] = RenderPassImage::RenderTarget(
				&rtMain_render,
				viewShadingInCS ? RenderPassImage::LoadOp::LOAD : RenderPassImage::LoadOp::CLEAR
			);
			rp[rp_count++] = RenderPassImage::DepthStencil(
				&depthBufferMain,
				RenderPassImage::LoadOp::LOAD,
				RenderPassImage::StoreOp::STORE,
				ResourceState::DEPTHSTENCIL,
				ResourceState::DEPTHSTENCIL,
				ResourceState::DEPTHSTENCIL
			);
			if (msaaSampleCount > 1)
			{
				rp[rp_count++] = RenderPassImage::Resolve(&rtMain);
			}
			if (device->CheckCapability(GraphicsDeviceCapability::VARIABLE_RATE_SHADING_TIER2) && rtShadingRate.IsValid())
			{
				rp[rp_count++] = RenderPassImage::ShadingRateSource(&rtShadingRate, ResourceState::UNORDERED_ACCESS, ResourceState::UNORDERED_ACCESS);
			}
			device->RenderPassBegin(rp, rp_count, cmd, RenderPassFlags::ALLOW_UAV_WRITES);

			if (!viewShadingInCS)
			{
				auto range = profiler::BeginRangeGPU("Opaque Scene", (CommandList*)&cmd);

				// Foreground:
				vp.min_depth = 1 - foregroundDepthRange;
				vp.max_depth = 1;
				device->BindViewports(1, &vp, cmd);
				DrawScene(
					viewMain,
					RENDERPASS_MAIN,
					cmd,
					renderer::DRAWSCENE_OPAQUE |
					renderer::DRAWSCENE_FOREGROUND_ONLY 
				);

				// Regular:
				vp.min_depth = 0;
				vp.max_depth = 1;
				device->BindViewports(1, &vp, cmd);
				DrawScene(
					viewMain,
					RENDERPASS_MAIN,
					cmd,
					drawscene_regular_flags
				);
				profiler::EndRange(range); // Opaque Scene
			}

			//RenderOutline(cmd);

			device->RenderPassEnd(cmd);

			//if (renderer::GetRaytracedShadowsEnabled() || renderer::GetScreenSpaceShadowsEnabled())
			//{
			//	GPUBarrier barrier = GPUBarrier::Image(&rtShadow, ResourceState::SHADER_RESOURCE, rtShadow.desc.layout);
			//	device->Barrier(&barrier, 1, cmd);
			//}
			//
			//if (rtAO.IsValid())
			//{
			//	device->Barrier(GPUBarrier::Aliasing(&rtAO, &rtParticleDistortion), cmd);
			//}

			device->EventEnd(cmd);
			});

		// Transparents, post processes, etc:
		cmd = device->BeginCommandList();
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(
				*camera,
				cameraPrevious,
				cameraReflection,
				cmd
			);
			BindCommonResources(cmd);

			//RenderLightShafts(cmd);
			
			//RenderVolumetrics(cmd);
			
			//RenderTransparents(cmd);

			RenderDirectVolumes(cmd);

			// Depth buffers expect a non-pixel shader resource state as they are generated on compute queue:
			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&rtLinearDepth, ResourceState::SHADER_RESOURCE, rtLinearDepth.desc.layout),
					GPUBarrier::Image(&depthBuffer_Copy, ResourceState::SHADER_RESOURCE, depthBuffer_Copy.desc.layout),
					GPUBarrier::Image(&debugUAV, ResourceState::UNORDERED_ACCESS, debugUAV.desc.layout),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}
		});

		if (isWetmapRefreshEnabled)
		{
			CommandList wetmap_cmd = device->BeginCommandList(QUEUE_COMPUTE);
			device->WaitCommandList(wetmap_cmd, cmd); // wait for transparents, it will be scheduled with late frame (GUI, etc)
			// Note: GPU processing of this compute task can overlap with beginning of the next frame because no one is waiting for it
			jobsystem::Execute(ctx, [this, wetmap_cmd](jobsystem::JobArgs args) {
				RefreshWetmaps(viewMain, wetmap_cmd);
				});
		}

		cmd = device->BeginCommandList();
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {
			RenderPostprocessChain(cmd);
			TextureStreamingReadbackCopy(*scene, cmd);
		});

		jobsystem::Wait(ctx);

		firstFrame = false;
		return true;
	}

	bool GRenderPath3DDetails::Destroy()
	{
		device->WaitForGPU();

		temporalAAResources = {};
		tiledLightResources = {};
		//tiledLightResources_planarReflection = {};
		luminanceResources = {};
		bloomResources = {};

		rtShadingRate = {};

		debugUAV = {};
		rtPostprocess = {};

		rtMain = {};
		rtMain_render = {};
		rtPrimitiveID_1 = {};
		rtPrimitiveID_2 = {};
		rtPrimitiveID_1_render = {};
		rtPrimitiveID_2_render = {};
		rtPrimitiveID_debug = {};
		
		depthBufferMain = {};
		rtLinearDepth = {};
		depthBuffer_Copy = {};
		depthBuffer_Copy1 = {};

		viewResources = {};

		rtParticleDistortion_render = {};
		rtParticleDistortion = {};

		distortion_overlay = {};

		return true;
	}
}

namespace vz
{
	GRenderPath3D* NewGRenderPath(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
	{
		return new GRenderPath3DDetails(vp, swapChain, rtRenderFinal);
	}
	void AddDeferredMIPGen(const Texture& texture, bool preserve_coverage)
	{
		rcommon::deferredResourceLock.lock();
		rcommon::deferredMIPGens.push_back(std::make_pair(texture, preserve_coverage));
		rcommon::deferredResourceLock.unlock();
	}
	void AddDeferredBlockCompression(const graphics::Texture& texture_src, const graphics::Texture& texture_bc)
	{
		rcommon::deferredResourceLock.lock();
		rcommon::deferredBCQueue.push_back(std::make_pair(texture_src, texture_bc));
		rcommon::deferredResourceLock.unlock();
	}
	void AddDeferredTextureCopy(const graphics::Texture& texture_src, const graphics::Texture& texture_dst, const bool mipGen)
	{
		if (!texture_src.IsValid() || !texture_dst.IsValid())
		{
			return;
		}
		rcommon::deferredResourceLock.lock();
		rcommon::deferredTextureCopy.push_back(std::make_pair(texture_src, texture_dst));
		rcommon::deferredResourceLock.unlock();
	}
	void AddDeferredBufferUpdate(const graphics::GPUBuffer& buffer, const void* data, const uint64_t size, const uint64_t offset)
	{
		if (!buffer.IsValid() || data == nullptr)
		{
			return;
		}
		size_t update_size = std::min(buffer.desc.size, size);
		if (update_size == 0)
		{
			return;
		}
		uint8_t* data_ptr = (uint8_t*)data;
		rcommon::deferredResourceLock.lock();
		rcommon::deferredBufferUpdate.push_back(std::make_pair(buffer, std::make_pair(data_ptr + offset, update_size)));
		rcommon::deferredResourceLock.unlock();
	}
}

