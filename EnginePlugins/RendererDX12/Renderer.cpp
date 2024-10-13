#include "Renderer.h"

#include "TextureHelper.h"

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
	Texture				textures[TEXTYPE_COUNT];

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
	const bool isFSREnabled = false;

	using namespace primitive;

	struct View
	{
		// User fills these:
		uint8_t layerMask = ~0;
		const Scene* scene = nullptr;
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
				//assert(!renderable.IsDirty());

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
		frameCB.texture_sheenlut_index = device->GetDescriptorIndex(&common::textures[TEXTYPE_2D_SHEENLUT], SubresourceType::SRV);

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
	struct TemporalAAResources
	{
		mutable int frame = 0;
		graphics::Texture textureTemporal[2];

		bool IsValid() const { return textureTemporal[0].IsValid(); }
		const graphics::Texture* GetCurrent() const { return &textureTemporal[frame % arraysize(textureTemporal)]; }
		const graphics::Texture* GetHistory() const { return &textureTemporal[(frame + 1) % arraysize(textureTemporal)]; }
	};

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
		TemporalAAResources temporalAAResources;

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
			//cameraReflection->Reflect(viewMain.reflectionPlane);
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

		
		if (renderer::isTemporalAAEnabled)
		{
			const XMFLOAT4& halton = math::GetHaltonSequence(graphics::GetDevice()->GetFrameCount() % 256);
			camera->jitter.x = (halton.x * 2 - 1) / (float)internalResolution.x;
			camera->jitter.y = (halton.y * 2 - 1) / (float)internalResolution.y;
			if (cameraReflection)
			{
				cameraReflection->jitter.x = camera->jitter.x * 2;
				cameraReflection->jitter.y = camera->jitter.x * 2;
			}
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
			if (cameraReflection)
			{
				cameraReflection->jitter = XMFLOAT2(0, 0);
			}
			temporalAAResources = {};
		}

		// for updating Jitters
		viewMain.camera->UpdateMatrix();
		if (viewMain.isPlanarReflectionVisible)
		{
			cameraReflection->UpdateMatrix();
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
		//		desc.width = internalResolution.x;
		//		desc.height = internalResolution.y;
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
		temporalAAResources = {};

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

