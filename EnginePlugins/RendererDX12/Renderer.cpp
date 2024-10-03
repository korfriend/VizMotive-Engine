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
	struct View
	{
		// User fills these:
		uint32_t layerMask = ~0u;
		const Scene* scene = nullptr;
		const CameraComponent* camera = nullptr;
		enum FLAGS
		{
			EMPTY = 0,
			ALLOW_OBJECTS = 1 << 0,
			ALLOW_LIGHTS = 1 << 1,
			ALLOW_DECALS = 1 << 2,
			ALLOW_ENVPROBES = 1 << 3,
			ALLOW_EMITTERS = 1 << 4,
			ALLOW_OCCLUSION_CULLING = 1 << 5,
			ALLOW_SHADOW_ATLAS_PACKING = 1 << 6,

			ALLOW_EVERYTHING = ~0u
		};
		uint32_t flags = EMPTY;

		// vz::renderer::UpdateVisibility() fills these:
		primitive::Frustum frustum;
		std::vector<uint32_t> visibleObjects;
		std::vector<uint32_t> visibleDecals;
		std::vector<uint32_t> visibleEnvProbes;
		std::vector<uint32_t> visibleEmitters;
		std::vector<uint32_t> visibleLights;
		rectpacker::State shadow_packer;
		rectpacker::Rect rain_blocker_shadow_rect;
		std::vector<rectpacker::Rect> visibleLightShadowRects;

		std::atomic<uint32_t> object_counter;
		std::atomic<uint32_t> light_counter;

		vz::SpinLock locker;
		bool planar_reflection_visible = false;
		float closestRefPlane = std::numeric_limits<float>::max();
		XMFLOAT4 reflectionPlane = XMFLOAT4(0, 1, 0, 0);
		std::atomic_bool volumetriclight_request{ false };

		void Clear()
		{
			visibleObjects.clear();
			visibleLights.clear();
			visibleDecals.clear();
			visibleEnvProbes.clear();
			visibleEmitters.clear();

			object_counter.store(0);
			light_counter.store(0);

			closestRefPlane = std::numeric_limits<float>::max();
			planar_reflection_visible = false;
			volumetriclight_request.store(false);
		}

		bool IsRequestedPlanarReflections() const
		{
			return planar_reflection_visible;
		}
		bool IsRequestedVolumetricLights() const
		{
			return volumetriclight_request.load();
		}
	};


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

		//if (!GetFreezeCullingCameraEnabled()) // just for debug
		//{
		//	vis.frustum = vis.camera->frustum;
		//}

		//if (!GetOcclusionCullingEnabled() || GetFreezeCullingCameraEnabled())
		//{
		//	vis.flags &= ~View::ALLOW_OCCLUSION_CULLING;
		//}

		/*
		if (vis.flags & View::ALLOW_LIGHTS)
		{
			// Cull lights:
			const uint32_t light_loop = (uint32_t)std::min(vis.scene->aabb_lights.size(), vis.scene->lights.GetCount());
			vis.visibleLights.resize(light_loop);
			vis.visibleLightShadowRects.clear();
			vis.visibleLightShadowRects.resize(light_loop);
			jobsystem::Dispatch(ctx, light_loop, groupSize, [&](jobsystem::JobArgs args) {

				// Setup stream compaction:
				uint32_t& group_count = *(uint32_t*)args.sharedmemory;
				uint32_t* group_list = (uint32_t*)args.sharedmemory + 1;
				if (args.isFirstJobInGroup)
				{
					group_count = 0; // first thread initializes local counter
				}

				const AABB& aabb = vis.scene->aabb_lights[args.jobIndex];

				if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
				{
					const LightComponent& light = vis.scene->lights[args.jobIndex];
					if (!light.IsInactive())
					{
						// Local stream compaction:
						//	(also compute light distance for shadow priority sorting)
						group_list[group_count] = args.jobIndex;
						group_count++;
						if (light.IsVolumetricsEnabled())
						{
							vis.volumetriclight_request.store(true);
						}

						if (vis.flags & View::ALLOW_OCCLUSION_CULLING)
						{
							if (!light.IsStatic() && light.GetType() != LightComponent::DIRECTIONAL || light.occlusionquery < 0)
							{
								if (!aabb.intersects(vis.camera->Eye))
								{
									light.occlusionquery = vis.scene->queryAllocator.fetch_add(1); // allocate new occlusion query from heap
								}
							}
						}
					}
				}

				// Global stream compaction:
				if (args.isLastJobInGroup && group_count > 0)
				{
					uint32_t prev_count = vis.light_counter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						vis.visibleLights[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		if (vis.flags & View::ALLOW_OBJECTS)
		{
			// Cull objects:
			const uint32_t object_loop = (uint32_t)std::min(vis.scene->aabb_objects.size(), vis.scene->objects.GetCount());
			vis.visibleObjects.resize(object_loop);
			jobsystem::Dispatch(ctx, object_loop, groupSize, [&](jobsystem::JobArgs args) {

				// Setup stream compaction:
				uint32_t& group_count = *(uint32_t*)args.sharedmemory;
				uint32_t* group_list = (uint32_t*)args.sharedmemory + 1;
				if (args.isFirstJobInGroup)
				{
					group_count = 0; // first thread initializes local counter
				}

				const AABB& aabb = vis.scene->aabb_objects[args.jobIndex];

				if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
				{
					// Local stream compaction:
					group_list[group_count++] = args.jobIndex;

					const ObjectComponent& object = vis.scene->objects[args.jobIndex];
					Scene::OcclusionResult& occlusion_result = vis.scene->occlusion_results_objects[args.jobIndex];
					bool occluded = false;
					if (vis.flags & View::ALLOW_OCCLUSION_CULLING)
					{
						occluded = occlusion_result.IsOccluded();
					}

					if ((vis.flags & View::ALLOW_REQUEST_REFLECTION) && object.IsRequestPlanarReflection() && !occluded)
					{
						// Planar reflection priority request:
						float dist = math::DistanceEstimated(vis.camera->Eye, object.center);
						vis.locker.lock();
						if (dist < vis.closestRefPlane)
						{
							vis.closestRefPlane = dist;
							XMVECTOR P = XMLoadFloat3(&object.center);
							XMVECTOR N = XMVectorSet(0, 1, 0, 0);
							N = XMVector3TransformNormal(N, XMLoadFloat4x4(&vis.scene->matrix_objects[args.jobIndex]));
							N = XMVector3Normalize(N);
							XMVECTOR _refPlane = XMPlaneFromPointNormal(P, N);
							XMStoreFloat4(&vis.reflectionPlane, _refPlane);

							vis.planar_reflection_visible = true;
						}
						vis.locker.unlock();
					}

					if (vis.flags & View::ALLOW_OCCLUSION_CULLING)
					{
						if (object.IsRenderable() && occlusion_result.occlusionQueries[vis.scene->queryheap_idx] < 0)
						{
							if (aabb.intersects(vis.camera->Eye))
							{
								// camera is inside the instance, mark it as visible in this frame:
								occlusion_result.occlusionHistory |= 1;
							}
							else
							{
								occlusion_result.occlusionQueries[vis.scene->queryheap_idx] = vis.scene->queryAllocator.fetch_add(1); // allocate new occlusion query from heap
							}
						}
					}
				}

				// Global stream compaction:
				if (args.isLastJobInGroup && group_count > 0)
				{
					uint32_t prev_count = vis.object_counter.fetch_add(group_count);
					for (uint32_t i = 0; i < group_count; ++i)
					{
						vis.visibleObjects[prev_count + i] = group_list[i];
					}
				}

				}, sharedmemory_size);
		}

		if (vis.flags & View::ALLOW_DECALS)
		{
			// Note: decals must be appended in order for correct blending, must not use parallelization!
			jobsystem::Execute(ctx, [&](jobsystem::JobArgs args) {
				for (size_t i = 0; i < vis.scene->aabb_decals.size(); ++i)
				{
					const AABB& aabb = vis.scene->aabb_decals[i];

					if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
					{
						vis.visibleDecals.push_back(uint32_t(i));
					}
				}
				});
		}

		if (vis.flags & View::ALLOW_ENVPROBES)
		{
			// Note: probes must be appended in order for correct blending, must not use parallelization!
			jobsystem::Execute(ctx, [&](jobsystem::JobArgs args) {
				for (size_t i = 0; i < vis.scene->aabb_probes.size(); ++i)
				{
					const AABB& aabb = vis.scene->aabb_probes[i];

					if ((aabb.layerMask & vis.layerMask) && vis.frustum.CheckBoxFast(aabb))
					{
						vis.visibleEnvProbes.push_back((uint32_t)i);
					}
				}
				});
		}

		//if (vis.flags & View::ALLOW_EMITTERS)
		//{
		//	jobsystem::Execute(ctx, [&](jobsystem::JobArgs args) {
		//		// Cull emitters:
		//		for (size_t i = 0; i < vis.scene->emitters.GetCount(); ++i)
		//		{
		//			const EmittedParticleSystem& emitter = vis.scene->emitters[i];
		//			if (!(emitter.layerMask & vis.layerMask))
		//			{
		//				continue;
		//			}
		//			vis.visibleEmitters.push_back((uint32_t)i);
		//		}
		//		});
		//}

		jobsystem::Wait(ctx);

		// finalize stream compaction:
		vis.visibleObjects.resize((size_t)vis.object_counter.load());
		vis.visibleLights.resize((size_t)vis.light_counter.load());

		// Shadow atlas packing:
		if (IsShadowsEnabled() && (vis.flags & View::ALLOW_SHADOW_ATLAS_PACKING) && !vis.visibleLights.empty())
		{
			auto range = profiler::BeginRangeCPU("Shadowmap packing");
			float iterative_scaling = 1;

			while (iterative_scaling > 0.03f)
			{
				vis.shadow_packer.clear();
				if (vis.scene->weather.rain_amount > 0)
				{
					// Rain blocker:
					rectpacker::Rect rect = {};
					rect.id = -1;
					rect.w = rect.h = 128;
					vis.shadow_packer.add_rect(rect);
				}
				for (uint32_t lightIndex : vis.visibleLights)
				{
					const LightComponent& light = vis.scene->lights[lightIndex];
					if (light.IsInactive())
						continue;
					if (!light.IsCastingShadow() || light.IsStatic())
						continue;

					const float dist = math::Distance(vis.camera->Eye, light.position);
					const float range = light.GetRange();
					const float amount = std::min(1.0f, range / std::max(0.001f, dist)) * iterative_scaling;

					rectpacker::Rect rect = {};
					rect.id = int(lightIndex);
					switch (light.GetType())
					{
					case LightComponent::DIRECTIONAL:
						if (light.forced_shadow_resolution >= 0)
						{
							rect.w = light.forced_shadow_resolution * int(light.cascade_distances.size());
							rect.h = light.forced_shadow_resolution;
						}
						else
						{
							rect.w = int(max_shadow_resolution_2D * iterative_scaling) * int(light.cascade_distances.size());
							rect.h = int(max_shadow_resolution_2D * iterative_scaling);
						}
						break;
					case LightComponent::SPOT:
						if (light.forced_shadow_resolution >= 0)
						{
							rect.w = int(light.forced_shadow_resolution);
							rect.h = int(light.forced_shadow_resolution);
						}
						else
						{
							rect.w = int(max_shadow_resolution_2D * amount);
							rect.h = int(max_shadow_resolution_2D * amount);
						}
						break;
					case LightComponent::POINT:
						if (light.forced_shadow_resolution >= 0)
						{
							rect.w = int(light.forced_shadow_resolution) * 6;
							rect.h = int(light.forced_shadow_resolution);
						}
						else
						{
							rect.w = int(max_shadow_resolution_cube * amount) * 6;
							rect.h = int(max_shadow_resolution_cube * amount);
						}
						break;
					}
					if (rect.w > 8 && rect.h > 8)
					{
						vis.shadow_packer.add_rect(rect);
					}
				}
				if (!vis.shadow_packer.rects.empty())
				{
					if (vis.shadow_packer.pack(8192))
					{
						for (auto& rect : vis.shadow_packer.rects)
						{
							if (rect.id == -1)
							{
								// Rain blocker:
								if (rect.was_packed)
								{
									vis.rain_blocker_shadow_rect = rect;
								}
								else
								{
									vis.rain_blocker_shadow_rect = {};
								}
								continue;
							}
							uint32_t lightIndex = uint32_t(rect.id);
							rectpacker::Rect& lightrect = vis.visibleLightShadowRects[lightIndex];
							const LightComponent& light = vis.scene->lights[lightIndex];
							if (rect.was_packed)
							{
								lightrect = rect;

								// Remove slice multipliers from rect:
								switch (light.GetType())
								{
								case LightComponent::DIRECTIONAL:
									lightrect.w /= int(light.cascade_distances.size());
									break;
								case LightComponent::POINT:
									lightrect.w /= 6;
									break;
								}
							}
						}

						if ((int)shadowMapAtlas.desc.width < vis.shadow_packer.width || (int)shadowMapAtlas.desc.height < vis.shadow_packer.height)
						{
							TextureDesc desc;
							desc.width = uint32_t(vis.shadow_packer.width);
							desc.height = uint32_t(vis.shadow_packer.height);
							desc.format = format_depthbuffer_shadowmap;
							desc.bind_flags = BindFlag::DEPTH_STENCIL | BindFlag::SHADER_RESOURCE;
							desc.layout = ResourceState::SHADER_RESOURCE;
							desc.misc_flags = ResourceMiscFlag::TEXTURE_COMPATIBLE_COMPRESSION;
							device->CreateTexture(&desc, nullptr, &shadowMapAtlas);
							device->SetName(&shadowMapAtlas, "shadowMapAtlas");

							desc.format = format_rendertarget_shadowmap;
							desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
							desc.layout = ResourceState::SHADER_RESOURCE;
							desc.clear.color[0] = 1;
							desc.clear.color[1] = 1;
							desc.clear.color[2] = 1;
							desc.clear.color[3] = 0;
							device->CreateTexture(&desc, nullptr, &shadowMapAtlas_Transparent);
							device->SetName(&shadowMapAtlas_Transparent, "shadowMapAtlas_Transparent");

						}

						break;
					}
					else
					{
						iterative_scaling *= 0.5f;
					}
				}
				else
				{
					iterative_scaling = 0.0; //PE: fix - endless loop if some lights do not have shadows.
				}
			}
			profiler::EndRange(range);
		}

		profiler::EndRange(range); // Frustum Culling
		/**/
	}
}

namespace vz
{
	struct GSceneDetails : GScene
	{
		GSceneDetails(Scene* scene) : GScene(scene) {}

		// note all GPU resources (their pointers) are managed by
		//  ComPtr or 
		//  RAII (Resource Acquisition Is Initialization) patterns

		// * note:: resources... 개별 할당된 상태...
		// * srv, uav... slot 이 bindless 한가...
		// * 여기선 scene 단위로 bindless resources
		// * - texture2D and texture 3D
		// * - constant buffers (instances)
		// * - vertex buffers ... , index buffer 만 IA 로 pipeline 에 적용

		// Instances for bindless renderables:
		//	contains in order:
		//		1) renderables (normal meshes)
		size_t instanceArraySize = 0;
		graphics::GPUBuffer instanceUploadBuffer[graphics::GraphicsDevice::GetBufferCount()]; // dynamic GPU-usage
		graphics::GPUBuffer instanceBuffer;	// default GPU-usage
		ShaderMeshInstance* instanceArrayMapped = nullptr; // CPU-access buffer pointer for instanceUploadBuffer[%2]

		// Materials for bindless visibility indexing:
		size_t materialArraySize = 0;
		graphics::GPUBuffer materialUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		graphics::GPUBuffer materialBuffer;
		graphics::GPUBuffer textureStreamingFeedbackBuffer;	// a sinlge UINT
		graphics::GPUBuffer textureStreamingFeedbackBuffer_readback[graphics::GraphicsDevice::GetBufferCount()];
		const uint32_t* textureStreamingFeedbackMapped = nullptr;
		ShaderMaterial* materialArrayMapped = nullptr;

		// 2. advanced version (based on WickedEngine)
		//ShaderMeshInstance* instanceArrayMapped = nullptr;
		//size_t instanceArraySize = 0;
		//graphics::GPUBuffer geometryUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		//graphics::GPUBuffer....
		//graphics::Texture....

		bool Update(const float dt) override;
		bool Destory() override;
	};

	bool GSceneDetails::Update(const float dt)
	{

		jobsystem::context ctx;

		GraphicsDevice* device = GetGraphicsDevice();

		// 1. dynamic rendering (such as particles and terrain, cloud...) kickoff
		// TODO

		// 2. constant buffers for renderables
		instanceArraySize = scene_->GetRenderableCount();
		if (instanceUploadBuffer[0].desc.size < (instanceArraySize * sizeof(ShaderMeshInstance)))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderMeshInstance);
			desc.size = desc.stride * instanceArraySize * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::SHADER_RESOURCE;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			// if CACHE_COHERENT_UMA is allowed, then use instanceUploadBuffer directly.
			// otherwise, use instanceBuffer.
			if (!device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
			{
				// Non-UMA: separate Default usage buffer
				device->CreateBuffer(&desc, nullptr, &instanceBuffer);
				device->SetName(&instanceBuffer, "GSceneDetails::instanceBuffer");

				// Upload buffer shouldn't be used by shaders with Non-UMA:
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
			}

			desc.usage = Usage::UPLOAD;
			for (int i = 0; i < arraysize(instanceUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &instanceUploadBuffer[i]);
				device->SetName(&instanceUploadBuffer[i], "GSceneDetails::instanceUploadBuffer");
			}
		}
		instanceArrayMapped = (ShaderMeshInstance*)instanceUploadBuffer[device->GetBufferIndex()].mapped_data;

		// 3. material buffers for shaders
		std::vector<Entity> mat_entities;
		std::vector<MaterialComponent*> mat_components;
		materialArraySize = compfactory::GetMaterialComponents(mat_entities, mat_components);
		if (materialUploadBuffer[0].desc.size < (materialArraySize * sizeof(ShaderMaterial)))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderMaterial);
			desc.size = desc.stride * materialArraySize * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::SHADER_RESOURCE;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			if (!device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
			{
				// Non-UMA: separate Default usage buffer
				device->CreateBuffer(&desc, nullptr, &materialBuffer);
				device->SetName(&materialBuffer, "GSceneDetails::materialBuffer");

				// Upload buffer shouldn't be used by shaders with Non-UMA:
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
			}

			desc.usage = Usage::UPLOAD;
			for (int i = 0; i < arraysize(materialUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &materialUploadBuffer[i]);
				device->SetName(&materialUploadBuffer[i], "GSceneDetails::materialUploadBuffer");
			}
		}
		materialArrayMapped = (ShaderMaterial*)materialUploadBuffer[device->GetBufferIndex()].mapped_data;

		if (textureStreamingFeedbackBuffer.desc.size < materialArraySize * sizeof(uint32_t))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(uint32_t);
			desc.size = desc.stride * materialArraySize * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::UNORDERED_ACCESS;
			desc.format = Format::R32_UINT;
			device->CreateBuffer(&desc, nullptr, &textureStreamingFeedbackBuffer);
			device->SetName(&textureStreamingFeedbackBuffer, "GSceneDetails::textureStreamingFeedbackBuffer");

			// Readback buffer shouldn't be used by shaders:
			desc.usage = Usage::READBACK;
			desc.bind_flags = BindFlag::NONE;
			desc.misc_flags = ResourceMiscFlag::NONE;
			for (int i = 0; i < arraysize(materialUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &textureStreamingFeedbackBuffer_readback[i]);
				device->SetName(&textureStreamingFeedbackBuffer_readback[i], "GSceneDetails::textureStreamingFeedbackBuffer_readback");
			}
		}
		textureStreamingFeedbackMapped = (const uint32_t*)textureStreamingFeedbackBuffer_readback[device->GetBufferIndex()].mapped_data;

		return true;
	}

	bool GSceneDetails::Destory()
	{
		return true;
	}

}

namespace vz
{
	struct GRenderPath3DDetails : GRenderPath3D
	{
		GRenderPath3DDetails(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal) : GRenderPath3D(swapChain, rtRenderFinal) {}

		// resources associated with render target buffers and textures

		bool ResizeCanvas() override; // must delete all canvas-related resources and re-create
		bool Render() override;
		bool Destory() override;
	};

	bool GRenderPath3DDetails::ResizeCanvas()
	{
		return true;
	}

	bool GRenderPath3DDetails::Render()
	{
		GraphicsDevice*& device = GetDevice();
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
	GScene* NewGScene(Scene* scene)
	{
		return new GSceneDetails(scene);
	}

	GRenderPath3D* NewGRenderPath(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
	{
		return new GRenderPath3DDetails(swapChain, rtRenderFinal);
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

