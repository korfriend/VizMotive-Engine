#include "Renderer.h"

#include "TextureHelper.h"

#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"
#include "Utils/Spinlock.h"
#include "Utils/Profiler.h"
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

	PipelineState		PSO_debug[DEBUGRENDERING_COUNT];
	PipelineState		PSO_render[RENDERPASS_COUNT];
	PipelineState		PSO_wireframe;
	PipelineState		PSO_occlusionquery;

	jobsystem::context	CTX_renderPSO[RENDERPASS_COUNT][MESH_SHADER_PSO_COUNT];
}

// TODO 
// move 
//	1. renderer options and functions to GRenderPath3DDetails
//	2. global parameters to vz::rcommon

namespace vz::renderer
{
	const float renderingSpeed = 1.f;
	const bool isOcclusionCullingEnabled = true;
	const bool isFreezeCullingCameraEnabled = false;
	const bool isSceneUpdateEnabled = true;
	const bool isTemporalAAEnabled = false;
	const bool isTessellationEnabled = false;
	const bool isFSREnabled = false;
	const bool isWireRender = false;
	const bool isDebugLightCulling = false;
	const bool isAdvancedLightCulling = false;
	const bool isMeshShaderAllowed = false;
	const bool isShadowsEnabled = false;
	const bool isVariableRateShadingClassification = false;

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

				const std::vector<Entity>& renderable_entities = view.scene->GetRenderableEntities();
				Entity entity = renderable_entities[args.jobIndex];
				const RenderableComponent& renderable = *compfactory::GetRenderableComponent(entity);
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
						if (renderable.IsRenderable() && occlusion_result.occlusionQueries[scene_Gdetails->queryheapIdx] < 0)
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

		frameCB.scene = scene_Gdetails->shaderscene;

		frameCB.texture_random64x64_index = device->GetDescriptorIndex(texturehelper::getRandom64x64(), SubresourceType::SRV);
		frameCB.texture_bluenoise_index = device->GetDescriptorIndex(texturehelper::getBlueNoise(), SubresourceType::SRV);
		frameCB.texture_sheenlut_index = device->GetDescriptorIndex(&rcommon::textures[TEXTYPE_2D_SHEENLUT], SubresourceType::SRV);

		uint lightarray_offset_directional = 0;
		uint lightarray_count_directional = 0;
		uint lightarray_offset = 0;
		uint lightarray_count = 0;

		LightEntity* light_entity_array = frameCB.lightArray;
		float4x4* light_matrix_array = frameCB.lightMatrixArray;

		uint32_t light_entity_counter = 0;

		const std::vector<Entity>& light_entities = vis.scene->GetLightEntities();

		// Write directional lights into entity array:
		lightarray_offset = light_entity_counter;
		lightarray_offset_directional = light_entity_counter;
		for (uint32_t lightIndex : vis.visibleLights)
		{
			if (light_entity_counter == LIGHT_ENTITY_COUNT)
			{
				light_entity_counter--;
				break;
			}

			const GLightComponent& light = *(GLightComponent*)compfactory::GetLightComponent(light_entities[lightIndex]);
			if (light.GetLightType() != LightComponent::LightType::DIRECTIONAL || light.IsInactive())
				continue;

			LightEntity light_entity = {};
			light_entity.layerMask = ~0u;

			light_entity.SetType(SCU32(light.GetLightType()));
			light_entity.position = light.position;
			light_entity.SetRange(light.GetRange());
			light_entity.SetRadius(0);
			light_entity.SetLength(0);
			light_entity.SetDirection(light.direction);
			XMFLOAT3 light_color = light.GetLightColor();
			float light_intensity = light.GetLightIntensity();
			light_entity.SetColor(float4(light_color.x * light_intensity, light_color.y * light_intensity, light_color.z * light_intensity, 1.f));

			// mark as no shadow by default:
			light_entity.indices = ~0;

			const uint cascade_count = std::min((uint)light.cascadeDistances.size(), LIGHT_ENTITY_COUNT - light_entity_counter);
			light_entity.SetShadowCascadeCount(cascade_count);

			//if (shadow && !light.cascade_distances.empty())
			//{
			//	SHCAM* shcams = (SHCAM*)alloca(sizeof(SHCAM) * cascade_count);
			//	CreateDirLightShadowCams(light, *vis.camera, shcams, cascade_count, shadow_rect);
			//	for (size_t cascade = 0; cascade < cascade_count; ++cascade)
			//	{
			//		XMStoreFloat4x4(&light_matrix_array[light_matrix_counter++], shcams[cascade].view_projection);
			//	}
			//}

			std::memcpy(light_entity_array + light_entity_counter, &light_entity, sizeof(LightEntity));
			light_entity_counter++;
			lightarray_count_directional++;
		}

		frameCB.directional_lights = LightEntityIterator(lightarray_offset_directional, lightarray_count_directional);
		frameCB.lights = LightEntityIterator(lightarray_offset, lightarray_count);
	}

	constexpr uint32_t CombineStencilrefs(MaterialComponent::StencilRef engineStencilRef, uint8_t userStencilRef)
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
		DRAWSCENE_DVR = 1 << 5, // only include objects that are tagged as foreground
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
		graphics::GPUBuffer tileFrustums; // entity culling frustums
		graphics::GPUBuffer entityTiles; // culled entity indices
	};
	struct LuminanceResources
	{
		graphics::GPUBuffer luminance;
	};


	struct WetmapPush
	{
		int wetmap;
		uint padding;
		uint geometryOffset;
		float rain_amount;
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

	using GBuffers = GGeometryComponent::GBuffers;
	using Primitive = GeometryComponent::Primitive;

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
		CameraCB cameraCB = {};
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
		//LuminanceResources luminanceResources; // dynamic allocation

		graphics::Texture rtShadingRate; // UINT8 shading rate per tile

		// aliased (rtPostprocess, rtPrimitiveID)

		graphics::Texture debugUAV; // debug UAV can be used by some shaders...
		graphics::Texture rtPostprocess; // ping-pong with main scene RT in post-process chain

		graphics::Texture rtMain;
		graphics::Texture rtMain_render; // can be MSAA
		graphics::Texture rtPrimitiveID;
		graphics::Texture rtPrimitiveID_render; // can be MSAA

		graphics::Texture depthBufferMain; // used for depth-testing, can be MSAA
		graphics::Texture rtLinearDepth; // linear depth result + mipchain (max filter)
		graphics::Texture depthBuffer_Copy; // used for shader resource, single sample
		graphics::Texture depthBuffer_Copy1; // used for disocclusion check

		ViewResources viewResources;	// dynamic allocation

		// progressive components
		SpinLock deferredMIPGenLock;
		std::vector<std::pair<Texture, bool>> deferredMIPGens;
		std::vector<std::pair<Texture, Texture>> deferredBCQueue; // BC : Block Compression

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
		void UpdateViewRes(const float dt);
		// Updates the GPU state according to the previously called UpdatePerFrameData()
		void UpdateRenderData(const renderer::View& view, const FrameCB& frameCB, CommandList cmd);
		void UpdateRenderDataAsync(const renderer::View& view, const FrameCB& frameCB, CommandList cmd);

		void RefreshLightmaps(const Scene& scene, CommandList cmd);
		
		void TextureStreamingReadbackCopy(const Scene& scene, graphics::CommandList cmd);

		void ProcessDeferredTextureRequests(CommandList cmd);
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
		void View_Prepare(const ViewResources& res, const Texture& input_primitiveID, CommandList cmd); // input_primitiveID can be MSAA
		// SURFACE need to be checked whether it requires FORWARD or DEFERRED
		void View_Surface(const ViewResources& res, const Texture& output, CommandList cmd); 
		void View_Surface_Reduced(const ViewResources& res, CommandList cmd);
		void View_Shade(const ViewResources& res, const Texture& output, CommandList cmd); 

		void DrawScene(const View& view, RENDERPASS renderPass, CommandList cmd, uint32_t flags);

		void RenderMeshes(const View& view, const RenderQueue& renderQueue, RENDERPASS renderPass, uint32_t filterMask, CommandList cmd, uint32_t flags = 0, uint32_t camera_count = 1);
		void RenderPostprocessChain(CommandList cmd);

		// ---------- GRenderPath3D's interfaces: -----------------
		bool ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight) override; // must delete all canvas-related resources and re-create
		bool Render(const float dt) override;
		bool Destroy() override;
	};
}

namespace vz
{
	void GRenderPath3DDetails::UpdateViewRes(const float dt)
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

		// for updating Jitters
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
	}

	void GRenderPath3DDetails::ProcessDeferredTextureRequests(CommandList cmd)
	{
		// TODO: paint texture...

		deferredMIPGenLock.lock();
		for (auto& it : deferredMIPGens)
		{
			MIPGEN_OPTIONS mipopt;
			mipopt.preserve_coverage = it.second;
			GenerateMipChain(it.first, MIPGENFILTER_LINEAR, cmd, mipopt);
		}
		deferredMIPGens.clear();
		for (auto& it : deferredBCQueue)
		{
			BlockCompress(it.first, it.second, cmd);
		}
		deferredBCQueue.clear();
		deferredMIPGenLock.unlock();
	}
	void GRenderPath3DDetails::GenerateMipChain(const Texture& texture, MIPGENFILTER filter, CommandList cmd, const MIPGEN_OPTIONS& options)
	{
		// TODO
	}
	void GRenderPath3DDetails::BlockCompress(const graphics::Texture& texture_src, graphics::Texture& texture_bc, graphics::CommandList cmd, uint32_t dst_slice_offset)
	{
		// TODO
		// const graphics::Texture& textureSrc, graphics::Texture& textureBC, graphics::CommandList cmd, uint32_t dstSliceOffset
	}
	void GRenderPath3DDetails::BindCameraCB(const CameraComponent& camera, const CameraComponent& cameraPrevious, const CameraComponent& cameraReflection, CommandList cmd)
	{
		cameraCB.Init();
		ShaderCamera& shadercam = cameraCB.cameras[0];

		// NOTE:
		//  the following parameters need to be set according to 
		//	* shadercam.options : RenderPath3D's property
		//  * shadercam.clip_plane  : RenderPath3D's property
		//  * shadercam.reflection_plane : Scene's property

		shadercam.options = SHADERCAMERA_OPTION_NONE;//camera.shadercamera_options;

		shadercam.view_projection = camera.GetViewProjection();
		shadercam.view = camera.GetView();
		shadercam.projection = camera.GetProjection();
		shadercam.position = camera.GetWorldEye();
		shadercam.inverse_view = camera.GetInvView();
		shadercam.inverse_projection = camera.GetInvProjection();
		shadercam.inverse_view_projection = camera.GetInvViewProjection();
		shadercam.forward = camera.GetWorldAt();
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

		/* TODO:
		shadercam.scissor.x = camera.scissor.left;
		shadercam.scissor.y = camera.scissor.top;
		shadercam.scissor.z = camera.scissor.right;
		shadercam.scissor.w = camera.scissor.bottom;

		// scissor_uv is also offset by 0.5 (half pixel) to avoid going over last pixel center with bilinear sampler:
		shadercam.scissor_uv.x = (shadercam.scissor.x + 0.5f) * shadercam.internal_resolution_rcp.x;
		shadercam.scissor_uv.y = (shadercam.scissor.y + 0.5f) * shadercam.internal_resolution_rcp.y;
		shadercam.scissor_uv.z = (shadercam.scissor.z - 0.5f) * shadercam.internal_resolution_rcp.x;
		shadercam.scissor_uv.w = (shadercam.scissor.w - 0.5f) * shadercam.internal_resolution_rcp.y;

		shadercam.entity_culling_tilecount = GetEntityCullingTileCount(shadercam.internal_resolution);
		shadercam.entity_culling_tile_bucket_count_flat = shadercam.entity_culling_tilecount.x * shadercam.entity_culling_tilecount.y * SHADER_ENTITY_TILE_BUCKET_COUNT;
		shadercam.sample_count = camera.sample_count;
		shadercam.visibility_tilecount = GetViewTileCount(shadercam.internal_resolution);
		shadercam.visibility_tilecount_flat = shadercam.visibility_tilecount.x * shadercam.visibility_tilecount.y;

		shadercam.texture_primitiveID_index = camera.texture_primitiveID_index;
		shadercam.texture_depth_index = camera.texture_depth_index;
		shadercam.texture_lineardepth_index = camera.texture_lineardepth_index;
		shadercam.texture_velocity_index = camera.texture_velocity_index;
		shadercam.texture_normal_index = camera.texture_normal_index;
		shadercam.texture_roughness_index = camera.texture_roughness_index;
		shadercam.buffer_entitytiles_index = camera.buffer_entitytiles_index;
		shadercam.texture_reflection_index = camera.texture_reflection_index;
		shadercam.texture_reflection_depth_index = camera.texture_reflection_depth_index;
		shadercam.texture_refraction_index = camera.texture_refraction_index;
		shadercam.texture_waterriples_index = camera.texture_waterriples_index;
		shadercam.texture_ao_index = camera.texture_ao_index;
		shadercam.texture_ssr_index = camera.texture_ssr_index;
		shadercam.texture_ssgi_index = camera.texture_ssgi_index;
		shadercam.texture_rtshadow_index = camera.texture_rtshadow_index;
		shadercam.texture_rtdiffuse_index = camera.texture_rtdiffuse_index;
		shadercam.texture_surfelgi_index = camera.texture_surfelgi_index;
		shadercam.texture_depth_index_prev = cameraPrevious.texture_depth_index;
		shadercam.texture_vxgi_diffuse_index = camera.texture_vxgi_diffuse_index;
		shadercam.texture_vxgi_specular_index = camera.texture_vxgi_specular_index;
		shadercam.texture_reprojected_depth_index = camera.texture_reprojected_depth_index;
		/**/
		device->BindDynamicConstantBuffer(cameraCB, CBSLOT_RENDERER_CAMERA, cmd);
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
				scene_Gdetails->instanceArraySize * sizeof(ShaderRenderable),
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
				scene_Gdetails->geometryArraySize * sizeof(ShaderGeometryPart),
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
		//	device->BindComputeShader(&shaders[CSTYPE_CAUSTICS], cmd);
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
		const std::vector<Entity> renderable_entities = view.scene->GetRenderableEntities();
		for (uint32_t renderableIndex = 0; renderableIndex < view.scene->GetRenderableCount(); ++renderableIndex)
		{
			const GRenderableComponent& renderable = *(GRenderableComponent*)compfactory::GetRenderableComponent(renderable_entities[renderableIndex]);
			if (!renderable.IsRenderable())
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
			for (size_t part_index = 0; part_index < num_parts; ++part_index)
			{
				GBuffers& part_buffers = *geomety.GetGBuffer(part_index);
				//GMaterialComponent& material = *(GMaterialComponent*)compfactory::GetMaterialComponent(renderable.GetMaterial(part_index));

				if (part_buffers.wetmapCleared || !part_buffers.wetmapBuffer.IsValid())
				{
					continue;
				}
				device->ClearUAV(&part_buffers.wetmapBuffer, 0, cmd);
				barrierStack.push_back(GPUBarrier::Buffer(&part_buffers.wetmapBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
				part_buffers.wetmapCleared = true;
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

				Entity entity = scene_Gdetails->renderableEntities[instanceIndex];
				GRenderableComponent& renderable = *(GRenderableComponent*)compfactory::GetRenderableComponent(entity);

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
				Entity entity = scene_Gdetails->lightEntities[lightIndex];
				const LightComponent& light = *compfactory::GetLightComponent(entity);

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

				const GRenderableComponent& renderable = view.scene->GetRenderableEntities()[instanceIndex];
				if (!renderable.IsRenderable())
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
					push.vb_pos_wind = mesh.vb_pos_wind.descriptor_srv;
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

		{
			GPUBufferDesc bd;
			bd.stride = sizeof(XMFLOAT4) * 4; // storing 4 planes for every tile
			bd.size = bd.stride * res.tileCount.x * res.tileCount.y;
			bd.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			bd.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			bd.usage = Usage::DEFAULT;
			device->CreateBuffer(&bd, nullptr, &res.tileFrustums);
			device->SetName(&res.tileFrustums, "tileFrustums");
		}
		{
			GPUBufferDesc bd;
			bd.stride = sizeof(uint);
			bd.size = res.tileCount.x * res.tileCount.y * bd.stride * SHADER_ENTITY_TILE_BUCKET_COUNT * 2; // *2: opaque and transparent arrays
			bd.usage = Usage::DEFAULT;
			bd.bind_flags = BindFlag::UNORDERED_ACCESS | BindFlag::SHADER_RESOURCE;
			bd.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			device->CreateBuffer(&bd, nullptr, &res.entityTiles);
			device->SetName(&res.entityTiles, "entityTiles");
		}
	}

	void GRenderPath3DDetails::ComputeTiledLightCulling(
		const TiledLightResources& res,
		const View& vis,
		const Texture& debugUAV,
		CommandList cmd
	)
	{
		auto range = profiler::BeginRangeGPU("Entity Culling", &cmd);

		// Initial barriers to put all resources into UAV:
		{
			GPUBarrier barriers[] = {
				GPUBarrier::Buffer(&res.tileFrustums, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
				GPUBarrier::Buffer(&res.entityTiles, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		if (
			vis.visibleLights.empty() //&&
			//vis.visibleDecals.empty() &&
			//vis.visibleEnvProbes.empty()
			)
		{
			device->EventBegin("Tiled Entity Clear Only", cmd);
			device->ClearUAV(&res.tileFrustums, 0, cmd);
			device->ClearUAV(&res.entityTiles, 0, cmd);
			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&res.tileFrustums, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
					GPUBarrier::Buffer(&res.entityTiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}
			device->EventEnd(cmd);
			profiler::EndRange(range);
			return;
		}

		BindCommonResources(cmd);

		// Frustum computation
		{
			device->EventBegin("Tile Frustums", cmd);
			device->BindComputeShader(&rcommon::shaders[CSTYPE_TILEFRUSTUMS], cmd);

			const GPUResource* uavs[] = {
				&res.tileFrustums
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			device->Dispatch(
				(res.tileCount.x + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE,
				(res.tileCount.y + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE,
				1,
				cmd
			);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&res.tileFrustums, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);
		}

		// Perform the culling
		{
			device->EventBegin("Entity Culling", cmd);

			device->BindResource(&res.tileFrustums, 0, cmd);

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
			desc.size = desc.stride * SCU32(MaterialComponent::ShaderType::COUNT);
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED | ResourceMiscFlag::INDIRECT_ARGS;
			bool success = device->CreateBuffer(&desc, nullptr, &res.bins);
			assert(success);
			device->SetName(&res.bins, "res.bins");

			desc.stride = sizeof(ViewTile);
			desc.size = desc.stride * res.tile_count.x * res.tile_count.y * SCU32(MaterialComponent::ShaderType::COUNT);
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
		const Texture& input_primitiveID, // can be MSAA
		CommandList cmd
	)
	{
		device->EventBegin("View_Prepare", cmd);
		auto range = profiler::BeginRangeGPU("View_Prepare", &cmd);

		BindCommonResources(cmd);

		// Note: the tile_count here must be valid whether the VisibilityResources was created or not!
		XMUINT2 tile_count = GetViewTileCount(XMUINT2(input_primitiveID.desc.width, input_primitiveID.desc.height));

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
			const bool msaa = input_primitiveID.GetDesc().sample_count > 1;

			device->BindResource(&input_primitiveID, 0, cmd);

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
			if (res.primitiveID_resolved)
			{
				device->BindUAV(res.primitiveID_resolved, 13, cmd);
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_resolved, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
			}
			else
			{
				device->BindUAV(&unbind, 13, cmd);
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
			if (res.primitiveID_resolved)
			{
				barrierStack.push_back(GPUBarrier::Image(res.primitiveID_resolved, ResourceState::UNORDERED_ACCESS, res.primitiveID_resolved->desc.layout));
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
				Entity geometry_entity = scene_Gdetails->geometryEntities[instancedBatch.geometryIndex];

				GGeometryComponent& geometry = *(GGeometryComponent*)compfactory::GetGeometryComponent(geometry_entity);

				if (!geometry.HasRenderData())
					return;

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
					GBuffers& part_buffer = *(GBuffers*)geometry.GetGBuffer(part_index);

					uint32_t material_index = instancedBatch.materialIndices[part_index];
					const GMaterialComponent& material = *(GMaterialComponent*)compfactory::GetMaterialComponent(scene_Gdetails->materialEntities[material_index]);

					
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

					RenderablePushConstants push;
					push.geometryIndex = geometry.geometryOffset + part_index;
					push.materialIndex = material_index;
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
			Entity renderable_entity = scene_Gdetails->renderableEntities[renderable_index];
			const GRenderableComponent& renderable = *(GRenderableComponent*)compfactory::GetRenderableComponent(renderable_entity);
			assert(renderable.IsRenderable());

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
				instancedBatch.instanceCount = 0;
				instancedBatch.dataOffset = (uint32_t)(instances.offset + instanceCount * sizeof(ShaderMeshInstancePointer));
				instancedBatch.forceAlphatestForDithering = 0;
				instancedBatch.aabb = AABB();
				instancedBatch.lod = renderable.lod;
				std::vector<Entity> materials = renderable.GetMaterials();
				instancedBatch.materialIndices.resize(materials.size());
				for (size_t i = 0, n = materials.size(); i < n; ++i)
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
				GPUBarrier::Aliasing(&rtPrimitiveID, &rtPostprocess),
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
			/*
			if (getEyeAdaptionEnabled())
			{
				renderer::ComputeLuminance(
					luminanceResources,
					rt_first == nullptr ? *rt_read : *rt_first,
					cmd,
					getEyeAdaptionRate(),
					getEyeAdaptionKey()
				);
			}
			if (getBloomEnabled())
			{
				renderer::ComputeBloom(
					bloomResources,
					rt_first == nullptr ? *rt_read : *rt_first,
					cmd,
					getBloomThreshold(),
					getExposure(),
					getEyeAdaptionEnabled() ? &luminanceResources.luminance : nullptr
				);
			}

			renderer::Postprocess_Tonemap(
				rt_first == nullptr ? *rt_read : *rt_first,
				*rt_write,
				cmd,
				getExposure(),
				getBrightness(),
				getContrast(),
				getSaturation(),
				getDitherEnabled(),
				getColorGradingEnabled() ? (scene->weather.colorGradingMap.IsValid() ? &scene->weather.colorGradingMap.GetTexture() : nullptr) : nullptr,
				&rtParticleDistortion,
				getEyeAdaptionEnabled() ? &luminanceResources.luminance : nullptr,
				getBloomEnabled() ? &bloomResources.texture_bloom : nullptr,
				colorspace,
				getTonemap(),
				&distortion_overlay
			);

			rt_first = nullptr;
			std::swap(rt_read, rt_write);
			/**/
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

			lastPostprocessRT = rt_read;

			// GUI Background blurring:
			{
				auto range = profiler::BeginRangeGPU("GUI Background Blur", cmd);
				device->EventBegin("GUI Background Blur", cmd);
				renderer::Postprocess_Downsample4x(*rt_read, rtGUIBlurredBackground[0], cmd);
				renderer::Postprocess_Downsample4x(rtGUIBlurredBackground[0], rtGUIBlurredBackground[2], cmd);
				renderer::Postprocess_Blur_Gaussian(rtGUIBlurredBackground[2], rtGUIBlurredBackground[1], rtGUIBlurredBackground[2], cmd, -1, -1, true);
				device->EventEnd(cmd);
				profiler::EndRange(range);
			}

			if (rtFSR[0].IsValid() && getFSREnabled())
			{
				renderer::Postprocess_FSR(*rt_read, rtFSR[1], rtFSR[0], cmd, getFSRSharpness());
				lastPostprocessRT = &rtFSR[0];
			}
			/**/
		}
	}
}

namespace vz
{
	// ---------- GRenderPath3D's interfaces: -----------------

	bool GRenderPath3DDetails::ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight)
	{
		if (canvasWidth_ == canvasWidth || canvasHeight_ == canvasHeight)
		{
			return true;
		}

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
			device->CreateTexture(&desc, nullptr, &rtPrimitiveID);
			device->SetName(&rtPrimitiveID, "rtPrimitiveID");

			if (msaaSampleCount > 1)
			{
				desc.sample_count = msaaSampleCount;
				desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
				desc.misc_flags = ResourceMiscFlag::NONE;
				device->CreateTexture(&desc, nullptr, &rtPrimitiveID_render);
				device->SetName(&rtPrimitiveID_render, "rtPrimitiveID_render");
			}
			else
			{
				rtPrimitiveID_render = rtPrimitiveID;
			}
		}
		{ // rtPostprocess
			TextureDesc desc;
			desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.format = FORMAT_rendertargetMain;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			assert(ComputeTextureMemorySizeInBytes(desc) <= ComputeTextureMemorySizeInBytes(rtPrimitiveID.desc)); // Aliased check
			device->CreateTexture(&desc, nullptr, &rtPostprocess, &rtPrimitiveID); // Aliased!
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
			desc.mip_levels = 5;
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
			desc.mip_levels = 5;
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

		firstFrame = true;

		return true;
	}

	bool GRenderPath3DDetails::Render(const float dt)
	{
		UpdateViewRes(dt);

		jobsystem::context ctx;

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
				GPUBarrier::Aliasing(&rtPostprocess, &rtPrimitiveID),
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

			});

		static const uint32_t drawscene_regular_flags = 
			renderer::DRAWSCENE_OPAQUE |
			renderer::DRAWSCENE_TESSELLATION |
			renderer::DRAWSCENE_OCCLUSIONCULLING;

		// Camera depth prepass + occlusion culling:
		cmd = device->BeginCommandList();
		CommandList cmd_maincamera_prepass = cmd;
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(*camera, cameraPrevious, cameraReflection, cmd);

			if (renderer::isOcclusionCullingEnabled)
			{
				OcclusionCulling_Reset(viewMain, cmd); // must be outside renderpass!
			}

			// TODO
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
					&rtPrimitiveID_render,
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

			if (isOcclusionCullingEnabled)
			{
				OcclusionCulling_Render(*camera, viewMain, cmd);
			}

			device->RenderPassEnd(cmd);

			if (isOcclusionCullingEnabled)
			{
				OcclusionCulling_Resolve(viewMain, cmd); // must be outside renderpass!
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
				rtPrimitiveID_render,
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
			//
			//RenderAO(cmd);

			//if (renderer::GetVariableRateShadingClassification() && device->CheckCapability(GraphicsDeviceCapability::VARIABLE_RATE_SHADING_TIER2))
			//{
			//	renderer::ComputeShadingRateClassification(
			//		rtShadingRate,
			//		debugUAV,
			//		cmd
			//	);
			//}
			//
			//RenderSSR(cmd);
			//
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
				CommandList cmd_tmp = cmd;
				auto range = profiler::BeginRangeGPU("Opaque Scene", &cmd_tmp);

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
			//
			//RenderVolumetrics(cmd);
			//
			//RenderTransparents(cmd);

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

		cmd = device->BeginCommandList();
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {
			RenderPostprocessChain(cmd);
			TextureStreamingReadbackCopy(*scene, cmd);
			});

		jobsystem::Wait(ctx);

		firstFrame = false;

		//RenderPassImage rp[] = {
		//	RenderPassImage::RenderTarget(&rtOut, RenderPassImage::LoadOp::CLEAR),
		//};
		//graphicsDevice_->RenderPassBegin(rp, arraysize(rp), cmd);
		//device->RenderPassBegin(&swapChain_, cmd);
		//device->RenderPassEnd(cmd);
		//device->SubmitCommandLists();
		/**/
		return true;
	}

	bool GRenderPath3DDetails::Destroy()
	{
		device->WaitForGPU();

		temporalAAResources = {};
		tiledLightResources = {};
		//tiledLightResources_planarReflection = {};
		//luminanceResources = {};

		rtShadingRate = {};

		debugUAV = {};
		rtPostprocess = {};

		rtMain = {};
		rtMain_render = {};
		rtPrimitiveID = {};
		rtPrimitiveID_render = {};
		
		depthBufferMain = {};
		rtLinearDepth = {};
		depthBuffer_Copy = {};
		depthBuffer_Copy1 = {};

		viewResources = {};

		return true;
	}
}

namespace vz
{
	GRenderPath3D* NewGRenderPath(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
	{
		return new GRenderPath3DDetails(vp, swapChain, rtRenderFinal);
	}

	bool InitRenderer()
	{
		Timer timer;

		initializer::SetUpStates();
		initializer::LoadBuffers();

		//static eventhandler::Handle handle2 = eventhandler::Subscribe(eventhandler::EVENT_RELOAD_SHADERS, [](uint64_t userdata) { LoadShaders(); });
		shader::LoadShaders();

		backlog::post("renderer Initialized (" + std::to_string((int)std::round(timer.elapsed())) + " ms)", backlog::LogLevel::Info);
		//initialized.store(true);
		return true;
	}

}

