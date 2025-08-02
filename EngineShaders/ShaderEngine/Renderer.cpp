#include "Renderer.h"
#include "RenderPath3D_Detail.h"
#include "Image.h"
#include "Font.h"

#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"
#include "Utils/Profiler.h"
#include "Utils/Helpers.h"
#include "Utils/vzMath.h"
#include "Utils/Geometrics.h"

#include "ThirdParty/RectPacker.h"

namespace vz::renderer
{
	thread_local std::vector<GPUBarrier> barrierStack;
	void BarrierStackFlush(CommandList cmd)
	{
		if (barrierStack.empty())
			return;
		graphics::GetDevice()->Barrier(barrierStack.data(), (uint32_t)barrierStack.size(), cmd);
		barrierStack.clear();
	}

	void GRenderPath3DDetails::UpdateProcess(const float dt)
	{
		scene_Gdetails = (GSceneDetails*)scene->GetGSceneHandle();

		// Frustum culling for main camera:
		visMain.layerMask = layerMask_;
		LayeredMaskComponent* layeredmask = camera->GetLayeredMaskComponent();
		if (layeredmask)
		{
			visMain.layerMask &= layeredmask->GetVisibleLayerMask();
		}
		visMain.camera = camera;
		
		visMain.flags = renderer::Visibility::ALLOW_EVERYTHING;
		if (!renderer::isOcclusionCullingEnabled || camera->IsSlicer())
		{
			visMain.flags &= ~renderer::Visibility::ALLOW_OCCLUSION_CULLING;
		}
		UpdateVisibility(visMain);

		// TODO
		if (visMain.isPlanarReflectionVisible)
		{
			// Frustum culling for planar reflections:
			cameraReflection = *camera;
			cameraReflection.jitter = XMFLOAT2(0, 0);
			//cameraReflection.Reflect(visMain.reflectionPlane);
			//viewReflection.layerMask = getLayerMask();
			visReflection.camera = &cameraReflection;
			visReflection.layerMask = layeredmaskReflection.GetVisibleLayerMask();
			visReflection.flags =
				//renderer::View::ALLOW_OBJECTS |
				//renderer::View::ALLOW_EMITTERS |
				//renderer::View::ALLOW_HAIRS |
				renderer::Visibility::ALLOW_LIGHTS;
			UpdateVisibility(visReflection);
		}

		XMUINT2 internalResolution = XMUINT2(canvasWidth_, canvasHeight_);

		UpdatePerFrameData(
			*scene,
			visMain,
			frameCB,
			renderer::isSceneUpdateEnabled ? dt : 0
		);


		// IMPORTANT NOTE:
		// ALL RESOURCES CREATED HERE MUST BE CHECKED BEFORE CREATION
		//	* resource IsValid() (MUST BE INVALID)
		//	* if the target OPTION is activated

		if (renderer::isTemporalAAEnabled && !camera->IsSlicer())
		{
			// updated at every frame (different values)
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

		if (scene_Gdetails->isOutlineEnabled && !rtOutlineSource.IsValid())
		{
			TextureDesc desc;
			desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
			desc.format = Format::R32_FLOAT;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			device->CreateTexture(&desc, nullptr, &rtOutlineSource);
			device->SetName(&rtOutlineSource, "rtOutlineSource");
		}
		else
		{
			rtOutlineSource = {};
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
		visMain.camera->UpdateMatrix();

		if (visMain.isPlanarReflectionVisible)
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
			visibilityShadingInCS// ||
			//getSSREnabled() ||
			//getSSGIEnabled() ||
			//getRaytracedReflectionEnabled() ||
			//getRaytracedDiffuseEnabled() ||
			//GetScreenSpaceShadowsEnabled() ||
			//GetRaytracedShadowsEnabled() ||
			//GetVXGIEnabled()
			)
		{
			if (!visibilityResources.IsValid())
			{
				CreateVisibilityResources(visibilityResources, internalResolution);
			}
		}
		else
		{
			visibilityResources = {};
		}

		// Keep a copy of last frame's depth buffer for temporal disocclusion checks, so swap with current one every frame:
		std::swap(depthBuffer_Copy, depthBuffer_Copy1);

		visibilityResources.depthbuffer = &depthBuffer_Copy;
		visibilityResources.lineardepth = &rtLinearDepth;
		if (msaaSampleCount > 1)
		{
			visibilityResources.primitiveID_1_resolved = &rtPrimitiveID_1;
			visibilityResources.primitiveID_2_resolved = &rtPrimitiveID_2;
		}
		else
		{
			visibilityResources.primitiveID_1_resolved = nullptr;
			visibilityResources.primitiveID_2_resolved = nullptr;
		}

		cameraReflectionPrevious = cameraReflection;
		cameraPrevious = *camera;
	}

	// based on graphics pipeline : Draw[...]
	void GRenderPath3DDetails::DrawScene(const Visibility& vis, RENDERPASS renderPass, CommandList cmd, uint32_t flags)
	{
		const bool opaque = flags & DRAWSCENE_OPAQUE;
		const bool transparent = flags & DRAWSCENE_TRANSPARENT;
		const bool occlusion = (flags & DRAWSCENE_OCCLUSIONCULLING) && (vis.flags & Visibility::ALLOW_OCCLUSION_CULLING) && isOcclusionCullingEnabled;
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
			renderQueue.init();
			for (uint32_t instanceIndex : vis.visibleRenderables_Mesh)
			{
				if (occlusion && scene_Gdetails->occlusionResultsObjects[instanceIndex].IsOccluded())
					continue;
				
				const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];
				assert(renderable.GetRenderableType() == RenderableType::MESH_RENDERABLE);
				
				if (foreground != renderable.IsForeground())
					continue;
				if (!renderable.layeredmask->IsVisibleWith(vis.layerMask))
					continue;
				if ((renderable.materialFilterFlags & filterMask) == 0)
					continue;

				if (renderable.materialFilterFlags & GMaterialComponent::MaterialFilterFlags::FILTER_GAUSSIAN_SPLATTING)
					continue;

				const float distance = math::Distance(vis.camera->GetWorldEye(), renderable.GetAABB().getCenter());
				if (distance > renderable.GetFadeDistance() + renderable.GetAABB().getRadius())
					continue;

				renderQueue.add(renderable.geometry->geometryIndex, instanceIndex, distance, renderable.sortBits);
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
				RenderMeshes(vis, renderQueue, renderPass, filterMask, cmd, flags);
			}
		}

		device->BindShadingRate(ShadingRate::RATE_1X1, cmd);
		device->EventEnd(cmd);

	}

	void GRenderPath3DDetails::TextureStreamingReadbackCopy(const Scene& scene, graphics::CommandList cmd)
	{
		if (scene_Gdetails->textureStreamingFeedbackBuffer.IsValid())
		{
			//device->WaitQueue(cmd, QUEUE_TYPE::QUEUE_COPY);
			//device->WaitQueue(cmd, QUEUE_TYPE::QUEUE_COMPUTE);

			device->Barrier(GPUBarrier::Buffer(&scene_Gdetails->textureStreamingFeedbackBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SRC), cmd);
			device->CopyResource(
				&scene_Gdetails->textureStreamingFeedbackBuffer_readback[device->GetBufferIndex()],
				&scene_Gdetails->textureStreamingFeedbackBuffer,
				cmd
			);
		}
	}

	void GRenderPath3DDetails::RenderMeshes(const Visibility& vis, const RenderQueue& renderQueue, RENDERPASS renderPass, uint32_t filterMask, CommandList cmd, uint32_t flags, uint32_t camera_count)
	{
		if (renderQueue.empty())
			return;

		device->EventBegin("RenderMeshes", cmd);

		// Always wait for non-mesh shader variants, it can be used when mesh shader is not applicable for an object:
		jobsystem::Wait(CTX_renderPSO[renderPass][MESH_SHADER_PSO_DISABLED]);

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
			jobsystem::Wait(CTX_renderPSO[renderPass][MESH_SHADER_PSO_ENABLED]);
		}

		// Pre-allocate space for all the instances in GPU-buffer:
		const size_t alloc_size = renderQueue.size() * camera_count * sizeof(ShaderMeshInstancePointer);
		const GraphicsDevice::GPUAllocation instances = device->AllocateGPU(alloc_size, cmd);
		const int instanceBufferDescriptorIndex = device->GetDescriptorIndex(&instances.buffer, SubresourceType::SRV);

		InstancedBatch instancedBatch = {};

		uint32_t prev_stencilref = SCU32(StencilRef::STENCILREF_DEFAULT);
		device->BindStencilRef(prev_stencilref, cmd);

		const GPUBuffer* prev_ib = nullptr;

		// This will be called every time we start a new draw call:
		//	calls draw per a geometry part
		auto BatchDrawingFlush = [&]()
			{
				if (instancedBatch.instanceCount == 0)
					return;

				//if (!geometry.HasRenderData())
				//{
				//	return;
				//}

				GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instancedBatch.renderableIndex];
				GGeometryComponent& geometry = *renderable.geometry;

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
				assert(parts.size() == renderable.materials.size());
				for (uint32_t part_index = 0, num_parts = parts.size(); part_index < num_parts; ++part_index)
				{
					const Primitive& part = parts[part_index];
					const GMaterialComponent& material = *renderable.materials[part_index];

					if (!part.HasRenderData())
					{
						//if (renderPass == RENDERPASS_MAIN)
						//{
						//	XMFLOAT4X4 world_matrix = scene->GetRenderableWorldMatrices()[instancedBatch.renderableIndex];
						//	renderableShapes.AddPrimitivePart(part, material.GetBaseColor(), world_matrix);
						//}
						continue;
					}

					GPrimBuffers& part_buffer = *(GPrimBuffers*)geometry.GetGPrimBuffer(part_index);

					if (part.GetPrimitiveType() == GeometryComponent::PrimitiveType::LINES)
					{
						if (renderPass != RENDERPASS_MAIN)
						{
							continue;
						}

						MiscCB sb;

						XMMATRIX W = XMLoadFloat4x4(&scene->GetRenderableWorldMatrices()[instancedBatch.renderableIndex]);
						XMMATRIX VP = XMLoadFloat4x4(&camera->GetViewProjection());
						XMMATRIX WVP = W * VP;
						XMStoreFloat4x4(&sb.g_xTransform, WVP);
						sb.g_xColor = XMFLOAT4(1, 1, 1, 1);
						sb.g_xThickness = 1.3f;// depthLineThicknessPixel;
						device->BindDynamicConstantBuffer(sb, CB_GETBINDSLOT(MiscCB), cmd);

						MeshPushConstants push;
						push.geometryIndex = geometry.geometryOffset + part_index;
						push.materialIndex = material.materialIndex;
						push.instBufferResIndex = renderable.resLookupOffset + part_index;
						push.instances = instanceBufferDescriptorIndex;
						push.instance_offset = (uint)instancedBatch.dataOffset;

						device->BindPipelineState(&PSO_RenderableShapes[MESH_RENDERING_LINES_DEPTH], cmd);
						device->PushConstants(&push, sizeof(push), cmd);
						device->BindIndexBuffer(&part_buffer.generalBuffer, geometry.GetIndexFormat(part_index), part_buffer.ib.offset, cmd);
						device->DrawIndexed(part.GetNumIndices(), 0, 0, cmd);
					}
					
					if (material.GetAlphaRef() < 1)
					{
						forceAlphaTestForDithering = 1;
					}

					//if (skip_planareflection_objects && material.HasPlanarReflection())
					//	continue;

					bool is_renderable = filterMask & material.GetFilterMaskFlags();

					if (shadow_rendering)
					{
						is_renderable = is_renderable && material.IsShadowCast();
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
									//pso = tessellatorRequested ? &PSO_object_wire_tessellation : &PSO_wireframe;
									pso = &PSO_wireframe;
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

							pso = GetObjectPSO(variant);
							assert(pso->IsValid());

							if ((filterMask & GMaterialComponent::FILTER_TRANSPARENT) && variant.bits.cullmode == (uint32_t)CullMode::NONE)
							{
								variant.bits.cullmode = (uint32_t)CullMode::FRONT;
								pso_backside = GetObjectPSO(variant);
							}
						}
					}

					if (pso == nullptr || !pso->IsValid())
					{
						continue;
					}

					const bool is_meshshader_pso = pso->desc.ms != nullptr;
					uint32_t stencilRef = material.GetUserStencilRef();// CombineStencilrefs(material.GetStencilRef(), material.GetUserStencilRef());
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
					push.materialIndex = material.materialIndex;
					push.instBufferResIndex = renderable.resLookupOffset + part_index;
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
			assert(renderable.GetRenderableType() == RenderableType::MESH_RENDERABLE);

			// TODO.. 
			//	to implement multi-instancing
			//	here, apply instance meta information
			//		e.g., AABB, transforms, colors, ...
			const AABB& instanceAABB = renderable.GetAABB();
			const uint8_t lod = batch.lod_override == 0xFF ? (uint8_t)renderable.lod : batch.lod_override;

			// When we encounter a new mesh inside the global instance array, we begin a new RenderBatch:
			if (geometry_index != instancedBatch.geometryIndex ||
				lod != instancedBatch.lod
				)
			{
				BatchDrawingFlush();

				instancedBatch = {};
				instancedBatch.geometryIndex = geometry_index;
				instancedBatch.renderableIndex = renderable_index;
				instancedBatch.instanceCount = 0;	
				instancedBatch.dataOffset = (uint32_t)(instances.offset + instanceCount * sizeof(ShaderMeshInstancePointer));
				instancedBatch.forceAlphatestForDithering = 0;
				instancedBatch.aabb = AABB();
				instancedBatch.lod = lod;
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

			if (renderable.GetAlphaRef() < 1.f)
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

	void GRenderPath3DDetails::RenderTransparents(CommandList cmd)
	{
		// Water ripple rendering:
		// TODO

		if (renderer::isFSREnabled)
		{
			// Save the pre-alpha for FSR2 reactive mask:
			//	Note that rtFSR temp resource is always larger or equal to rtMain, so CopyTexture is used instead of CopyResource!
			//GPUBarrier barriers[] = {
			//	GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::COPY_SRC),
			//	GPUBarrier::Image(&rtFSR[1], rtFSR->desc.layout, ResourceState::COPY_DST),
			//};
			//device->Barrier(barriers, arraysize(barriers), cmd);
			//device->CopyTexture(
			//	&rtFSR[1], 0, 0, 0, 0, 0,
			//	&rtMain, 0, 0,
			//	cmd
			//);
			//for (int i = 0; i < arraysize(barriers); ++i)
			//{
			//	std::swap(barriers[i].image.layout_before, barriers[i].image.layout_after);
			//}
			//device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->BindScissorRects(1, &scissor, cmd);

		Viewport vp = viewport;
		//vp.width = (float)depthBufferMain.GetDesc().width;
		//vp.height = (float)depthBufferMain.GetDesc().height;
		vp.min_depth = 0;
		vp.max_depth = 1;
		device->BindViewports(1, &vp, cmd);

		RenderPassImage rp[] = {
			RenderPassImage::RenderTarget(&rtMain_render, RenderPassImage::LoadOp::LOAD),
			RenderPassImage::DepthStencil(
				&depthBufferMain,
				RenderPassImage::LoadOp::LOAD,
				RenderPassImage::StoreOp::STORE,
				ResourceState::DEPTHSTENCIL,
				ResourceState::DEPTHSTENCIL,
				ResourceState::DEPTHSTENCIL
			),
			RenderPassImage::Resolve(&rtMain),
		};

		// Draw only the ocean first, fog and lightshafts will be blended on top:
		// TODO

		if (visMain.IsTransparentsVisible())
		{
			//RenderSceneMIPChain(cmd);
		}

		device->RenderPassBegin(rp, msaaSampleCount > 1 ? 3 : 2, cmd);

		// Note: volumetrics and light shafts are blended before transparent scene, because they used depth of the opaques
		//	But the ocean is special, because it does have depth for them implicitly computed from ocean plane

		if (renderer::isVolumeLightsEnabled && visMain.IsRequestedVolumetricLights())
		{
			//device->EventBegin("Contribute Volumetric Lights", cmd);
			//Postprocess_Upsample_Bilateral(
			//	rtVolumetricLights,
			//	rtLinearDepth,
			//	rtMain,
			//	cmd,
			//	true,
			//	1.5f
			//);
			//device->EventEnd(cmd);
		}

		//XMVECTOR sunDirection = XMLoadFloat3(&scene->weather.sunDirection);
		//if (renderer::isLightShaftsEnabled() && XMVectorGetX(XMVector3Dot(sunDirection, camera->GetAt())) > 0)
		//{
		//	device->EventBegin("Contribute LightShafts", cmd);
		//	image::Params fx;
		//	fx.enableFullScreen();
		//	fx.blendFlag = BLENDMODE_ADDITIVE;
		//	image::Draw(&rtSun[1], fx, cmd);
		//	device->EventEnd(cmd);
		//}

		// Transparent scene
		if (visMain.IsTransparentsVisible())
		{
			auto range = profiler::BeginRangeGPU("Transparent Scene", &cmd);
			device->EventBegin("Transparent Scene", cmd);

			// Regular:
			vp.min_depth = 0;
			vp.max_depth = 1;
			device->BindViewports(1, &vp, cmd);
			DrawScene(
				visMain,
				RENDERPASS_MAIN,
				cmd,
				renderer::DRAWSCENE_TRANSPARENT |
				renderer::DRAWSCENE_TESSELLATION |
				renderer::DRAWSCENE_OCCLUSIONCULLING
			);

			// Foreground:
			vp.min_depth = 1 - foregroundDepthRange;
			vp.max_depth = 1;
			device->BindViewports(1, &vp, cmd);
			DrawScene(
				visMain,
				RENDERPASS_MAIN,
				cmd,
				renderer::DRAWSCENE_TRANSPARENT |
				renderer::DRAWSCENE_FOREGROUND_ONLY
			);

			// Reset normal viewport:
			vp.min_depth = 0;
			vp.max_depth = 1;
			device->BindViewports(1, &vp, cmd);

			device->EventEnd(cmd);
			profiler::EndRange(range); // Transparent Scene
		}

		//DrawDebugWorld(*scene, *camera, cmd);

		DrawLightVisualizers(visMain, cmd);

		//DrawSoftParticles(visMain, false, cmd);
		
		DrawSpritesAndFonts(*camera, false, cmd);

		if (renderer::isLensFlareEnabled)
		{
			//DrawLensFlares(
			//	visibility_main,
			//	cmd,
			//	scene->weather.IsVolumetricClouds() ? &volumetriccloudResources.texture_cloudMask : nullptr
			//);
		}

		device->RenderPassEnd(cmd);

		// Distortion particles:
		if (rtParticleDistortion.IsValid())
		{
			//if (rtWaterRipple.IsValid())
			//{
			//	device->Barrier(GPUBarrier::Aliasing(&rtWaterRipple, &rtParticleDistortion), cmd);
			//}

			if (msaaSampleCount > 1)
			{
				RenderPassImage rp[] = {
					RenderPassImage::RenderTarget(&rtParticleDistortion_render, RenderPassImage::LoadOp::CLEAR),
					RenderPassImage::DepthStencil(
						&depthBufferMain,
						RenderPassImage::LoadOp::LOAD,
						RenderPassImage::StoreOp::STORE,
						ResourceState::DEPTHSTENCIL,
						ResourceState::DEPTHSTENCIL,
						ResourceState::DEPTHSTENCIL
					),
					RenderPassImage::Resolve(&rtParticleDistortion)
				};
				device->RenderPassBegin(rp, arraysize(rp), cmd);
			}
			else
			{
				RenderPassImage rp[] = {
					RenderPassImage::RenderTarget(&rtParticleDistortion, RenderPassImage::LoadOp::CLEAR),
					RenderPassImage::DepthStencil(
						&depthBufferMain,
						RenderPassImage::LoadOp::LOAD,
						RenderPassImage::StoreOp::STORE,
						ResourceState::DEPTHSTENCIL,
						ResourceState::DEPTHSTENCIL,
						ResourceState::DEPTHSTENCIL
					),
				};
				device->RenderPassBegin(rp, arraysize(rp), cmd);
			}

			Viewport vp;
			vp.width = (float)rtParticleDistortion.GetDesc().width;
			vp.height = (float)rtParticleDistortion.GetDesc().height;
			device->BindViewports(1, &vp, cmd);

			//DrawSoftParticles(visibility_main, true, cmd);
			DrawSpritesAndFonts(*camera, true, cmd);

			device->RenderPassEnd(cmd);
		}

		Postprocess_Downsample4x(rtMain, rtSceneCopy, cmd);
	}

	bool GRenderPath3DDetails::ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight)
	{
		if (!camera || (canvasWidth_ == canvasWidth && canvasHeight_ == canvasHeight))
		{
			return true;
		}
		Destroy();

		firstFrame = true;

		canvasWidth_ = canvasWidth;
		canvasHeight_ = canvasHeight;
		XMUINT2 internalResolution(canvasWidth, canvasHeight);
		CreateTiledLightResources(tiledLightResources, internalResolution);

		if (camera->GetComponentType() == ComponentType::SLICER)
		{
			return ResizeCanvasSlicer(canvasWidth, canvasHeight);
		}
		
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
				device->SetName(&rtPrimitiveID_2_render, "rtPrimitiveID_2_render");
			}
			else
			{
				rtPrimitiveID_1_render = rtPrimitiveID_1;
				rtPrimitiveID_2_render = rtPrimitiveID_2;
			}
		}
		{
			if (!camera->IsSlicer())
			{
				TextureDesc desc;
				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.format = renderer::FORMAT_rendertargetMain;
				desc.width = internalResolution.x / 4;
				desc.height = internalResolution.y / 4;
				desc.mip_levels = std::min(8u, (uint32_t)std::log2(std::max(desc.width, desc.height)));
				device->CreateTexture(&desc, nullptr, &rtSceneCopy);
				device->SetName(&rtSceneCopy, "rtSceneCopy");
				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS | BindFlag::RENDER_TARGET; // render target for aliasing
				device->CreateTexture(&desc, nullptr, &rtSceneCopy_tmp, &rtPrimitiveID_1);
				device->SetName(&rtSceneCopy_tmp, "rtSceneCopy_tmp");

				for (uint32_t i = 0; i < rtSceneCopy.GetDesc().mip_levels; ++i)
				{
					int subresource_index;
					subresource_index = device->CreateSubresource(&rtSceneCopy, SubresourceType::SRV, 0, 1, i, 1);
					assert(subresource_index == i);
					subresource_index = device->CreateSubresource(&rtSceneCopy_tmp, SubresourceType::SRV, 0, 1, i, 1);
					assert(subresource_index == i);
					subresource_index = device->CreateSubresource(&rtSceneCopy, SubresourceType::UAV, 0, 1, i, 1);
					assert(subresource_index == i);
					subresource_index = device->CreateSubresource(&rtSceneCopy_tmp, SubresourceType::UAV, 0, 1, i, 1);
					assert(subresource_index == i);
				}

				// because this is used by SSR and SSGI before it gets a chance to be normally rendered, it MUST be cleared!
				CommandList cmd = device->BeginCommandList();
				device->Barrier(GPUBarrier::Image(&rtSceneCopy, rtSceneCopy.desc.layout, ResourceState::UNORDERED_ACCESS), cmd);
				device->ClearUAV(&rtSceneCopy, 0, cmd);
				device->Barrier(GPUBarrier::Image(&rtSceneCopy, ResourceState::UNORDERED_ACCESS, rtSceneCopy.desc.layout), cmd);
				device->SubmitCommandLists();
			}
			else
			{
				rtSceneCopy = {};
				rtSceneCopy_tmp = {};
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

		if (renderer::isGaussianSplattingEnabled)
		{
			CreateGaussianResources(gaussianSplattingResources, internalResolution);
		}
		//CreateScreenSpaceShadowResources(screenspaceshadowResources, internalResolution);

		return true;
	}

	bool GRenderPath3DDetails::ResizeCanvasSlicer(uint32_t canvasWidth, uint32_t canvasHeight)
	{
		// Call from ResizeCanvas!!!
		XMUINT2 internalResolution(canvasWidth, canvasHeight);

		// ----- Render targets:-----

		// HERE, MSAA is NOT PERMITTED!!!
		vzlog_assert(msaaSampleCount == 1, "Slicer does NOT ALLOW MSAA!!");
		{ // rtMain, rtMain_render
			TextureDesc desc;
			desc.format = FORMAT_rendertargetMain;
			desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			desc.sample_count = 1;
			device->CreateTexture(&desc, nullptr, &rtMain);
			device->SetName(&rtMain, "rtMain");

			rtMain_render = rtMain;
		}
				
		/// rtPrimitiveID_1: UINT - counter (8bit) / mask (8bit) / intermediate distance map (16but)
		/// rtPrimitiveID_2: R32G32B32A32_UINT - Layer_Packed0
		{ 
			TextureDesc desc;
			desc.format = Format::R32_UINT;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			desc.sample_count = 1;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			desc.misc_flags = ResourceMiscFlag::ALIASING_TEXTURE_RT_DS;
			device->CreateTexture(&desc, nullptr, &rtPrimitiveID_1);
			device->SetName(&rtPrimitiveID_1, "rtPrimitiveID_1");

			desc.format = Format::R32G32B32A32_UINT;
			device->CreateTexture(&desc, nullptr, &rtPrimitiveID_2);
			device->SetName(&rtPrimitiveID_2, "rtPrimitiveID_2");

			rtPrimitiveID_1_render = rtPrimitiveID_1;
			rtPrimitiveID_2_render = rtPrimitiveID_2;
		}

		{ // rtPostprocess
			TextureDesc desc;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.format = FORMAT_rendertargetMain;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			// the same size of format is recommended. the following condition (less equal) will cause some unexpected behavior.
			assert(ComputeTextureMemorySizeInBytes(desc) <= ComputeTextureMemorySizeInBytes(rtPrimitiveID_1.desc)); // Aliased check
			device->CreateTexture(&desc, nullptr, &rtPostprocess, &rtPrimitiveID_1); // Aliased!
			device->SetName(&rtPostprocess, "rtPostprocess");
		}

		//----- Depth buffers: -----
		// rtLinearDepth: R32G32_UINT - Layer_Packed1
		{
			TextureDesc desc;
			desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.format = Format::R32G32_UINT;
			desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
			desc.width = internalResolution.x;
			desc.height = internalResolution.y;
			desc.sample_count = msaaSampleCount; // MUST BE 1
			desc.mip_levels = 1;
			desc.array_size = 1;
			device->CreateTexture(&desc, nullptr, &rtLinearDepth); 
			device->SetName(&rtLinearDepth, "rtLinearDepth");
		}

		return true;
	}

	void GRenderPath3DDetails::Compose(CommandList cmd) 
	{
		auto range = profiler::BeginRangeCPU("Compose");

		image::SetCanvas(canvasWidth_, canvasHeight_, 96.F);
		font::SetCanvas(canvasWidth_, canvasHeight_, 96.F);

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
			case DEBUG_BUFFER::CASCADE_SHADOW_MAP:
				fx.disableDebugTest();
				image::Draw(lastPostprocessRT, fx, cmd);
				{
					fx.enableDebugTest();
					image::Params fx_sm = fx;
					fx_sm.disableFullScreen();
					fx_sm.pos = XMFLOAT3(canvasWidth_ * 0.75f, canvasHeight_ * 0.75f, 0);
					fx_sm.size = XMFLOAT2(canvasWidth_ * 0.25f, canvasHeight_ * 0.25f);
					image::Draw(&shadowMapAtlas, fx_sm, cmd);
				}
				{
					font::Params fx_font;
					fx_font.disableDepthTest();
					fx_font.posX = canvasWidth_ * 0.76f;
					fx_font.posY = canvasHeight_ * 0.76f;
					fx_font.bolden = 0.1f;
					fx_font.shadowColor = Color(255, 255, 0, 150);
					font::Draw("Shadow Map", fx_font, cmd);
				}
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
		if (!renderer::IsInitialized())
		{
			return false;
		}

		if (!rtMain.IsValid())
		{
			ResizeCanvas(canvasWidth_, canvasHeight_);
		}

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
		if (camera->GetComponentType() == ComponentType::SLICER)
		{
			SlicerProcess();
		}
		else
		{
			vzlog_assert(camera->GetComponentType() == ComponentType::CAMERA, "RenderProcess requires CAMERA component!!");
			RenderProcess();
		}
		profiler::EndRange(range);

		graphics::CommandList cmd = device->BeginCommandList();
		// Begin final compositing:
		graphics::Viewport viewport_composite; // full buffer
		viewport_composite.width = (float)canvasWidth_;
		viewport_composite.height = (float)canvasHeight_;
		device->BindViewports(1, &viewport_composite, cmd);

		if (rtRenderFinal_.IsValid())
		{
			graphics::RenderPassImage rp[] = {
				graphics::RenderPassImage::RenderTarget(&rtRenderFinal_, graphics::RenderPassImage::LoadOp::LOAD),//CLEAR
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

		return true;
	}

	bool GRenderPath3DDetails::RenderProcess()
	{
		jobsystem::context ctx;

		// Preparing the frame:
		CommandList cmd = device->BeginCommandList();
		device->WaitQueue(cmd, QUEUE_COMPUTE); // sync to prev frame compute (disallow prev frame overlapping a compute task into updating global scene resources for this frame)
		ProcessDeferredResourceRequests(cmd); // Execute it first thing in the frame here, on main thread, to not allow other thread steal it and execute on different command list!
		
		CommandList cmd_prepareframe = cmd;
		// remember GraphicsDevice::BeginCommandList does incur some overhead
		//	this is why jobsystem::Execute is used here
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(*camera, cameraPrevious, cameraReflection, cmd);
			UpdateRenderData(visMain, frameCB, cmd);

			uint32_t num_barriers = 2;
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&debugUAV, debugUAV.desc.layout, ResourceState::UNORDERED_ACCESS),
				GPUBarrier::Aliasing(&rtPostprocess, &rtPrimitiveID_1),
				GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::SHADER_RESOURCE_COMPUTE), // prepares transition for discard in dx12
			};
			if (visibilityShadingInCS)
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
			UpdateRenderDataAsync(visMain, frameCB, cmd);

			if (isWetmapRefreshEnabled)
			{
				RefreshWetmaps(visMain, cmd);
			}

			// UpdateRaytracingAccelerationStructures
			// ComputeSkyAtmosphere
			// SurfelGI
			// DDGI

			});

		const uint32_t drawscene_regular_flags = 
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

			Viewport vp = viewport; // TODO.. viewport just for render-out result vs. viewport for enhancing performance...?!
			//vp.width = (float)depthBufferMain.GetDesc().width;
			//vp.height = (float)depthBufferMain.GetDesc().height;

			// ----- Foreground: -----
			vp.min_depth = 1.f - foregroundDepthRange;
			vp.max_depth = 1.f;
			device->BindViewports(1, &vp, cmd);
			DrawScene(
				visMain,
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
				visMain,
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

			Visibility_Prepare(
				visibilityResources,
				rtPrimitiveID_1_render,
				rtPrimitiveID_2_render,
				cmd
			);

			ComputeTiledLightCulling(
				tiledLightResources,
				visMain,
				debugUAV,
				cmd
			);

			if (visibilityShadingInCS)
			{
				Visibility_Surface(
					visibilityResources,
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
				
				OcclusionCulling_Reset(visMain, cmd); // must be outside renderpass!
								
				RenderPassImage rp[] = {
					RenderPassImage::DepthStencil(&depthBufferMain),
				};
				device->RenderPassBegin(rp, arraysize(rp), cmd);
				
				device->BindScissorRects(1, &scissor, cmd);
				
				Viewport vp = viewport;
				//vp.width = (float)depthBufferMain.GetDesc().width;
				//vp.height = (float)depthBufferMain.GetDesc().height;
				device->BindViewports(1, &vp, cmd);
				
				OcclusionCulling_Render(*camera, visMain, cmd);
				
				device->RenderPassEnd(cmd);

				OcclusionCulling_Resolve(visMain, cmd); // must be outside renderpass!

				device->EventEnd(cmd);
				});
		}
		
		// Shadow maps:
		if (renderer::isShadowsEnabled)
		{
			cmd = device->BeginCommandList();
			jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {
				DrawShadowmaps(visMain, cmd);
				});
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
			device->WaitCommandList(cmd, cmd_prepareframe_async);
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

			if (visibilityShadingInCS)
			{
				Visibility_Shade(
					visibilityResources,
					rtMain,
					cmd
				);
			}

			Viewport vp = viewport;
			//vp.width = (float)depthBufferMain.GetDesc().width;
			//vp.height = (float)depthBufferMain.GetDesc().height;
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
				visibilityShadingInCS || !clearEnabled ? RenderPassImage::LoadOp::LOAD : RenderPassImage::LoadOp::CLEAR
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

			if (!visibilityShadingInCS)
			{
				auto range = profiler::BeginRangeGPU("Opaque Scene", (CommandList*)&cmd);

				// Foreground:
				vp.min_depth = 1 - foregroundDepthRange;
				vp.max_depth = 1;
				device->BindViewports(1, &vp, cmd);
				DrawScene(
					visMain,
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
					visMain,
					RENDERPASS_MAIN,
					cmd,
					drawscene_regular_flags
				);
				profiler::EndRange(range); // Opaque Scene
			}

			//RenderOutline(cmd);
			if (renderer::isDebugShapeEnabled)
			{
				scene_Gdetails->debugShapes.DrawLines(*camera, cmd, false);
			}

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

		// Transparents, Special Renderers, and Post Processes, etc:
		cmd = device->BeginCommandList();
		//if (cmd_water.IsValid())
		//{
		//	device->WaitCommandList(cmd, cmd_ocean);
		//}
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {
		
			BindCameraCB(
				*camera,
				cameraPrevious,
				cameraReflection,
				cmd
			);
		
			//RenderLightShafts(cmd);
			
			//RenderVolumetrics(cmd);

			RenderTransparents(cmd);

			// Depth buffers expect a non-pixel shader resource state as they are generated on compute queue:
			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&rtLinearDepth, ResourceState::SHADER_RESOURCE, rtLinearDepth.desc.layout),
					GPUBarrier::Image(&depthBuffer_Copy, ResourceState::SHADER_RESOURCE, depthBuffer_Copy.desc.layout),
					GPUBarrier::Image(&debugUAV, ResourceState::UNORDERED_ACCESS, debugUAV.desc.layout),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			// Special Rendering Features
			//	Assuming the barrier states of the used resources are set to the 'default' barrier states

			if (renderer::isGaussianSplattingEnabled)
				RenderGaussianSplatting(cmd);

			RenderDirectVolumes(cmd);
			
		});

		// TODO : RenderToTexture
		// RenderCameraComponents(ctx);

		cmd = device->BeginCommandList();
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {
			RenderPostprocessChain(cmd);
			TextureStreamingReadbackCopy(*scene, cmd);
		});

		// IMPORTANT NOTE:
		//	* this is why JOBSYSTEM is used for rendering process
		//	* ONLY ONCE call of jobsystem::Wait(ctx)
		//	* CommandLists will be submitted sequentially w.r.t. device->BeginCommandList 
		jobsystem::Wait(ctx);	

		firstFrame = false;
		return true;
	}

	bool GRenderPath3DDetails::SlicerProcess()
	{
		jobsystem::context ctx;

		// Preparing the frame:
		CommandList cmd = device->BeginCommandList();
		// DO NOT 'device->WaitQueue(cmd, QUEUE_COMPUTE)' when there is NO QUEUE_COMPUTE commmand list!
		//device->WaitQueue(cmd, QUEUE_COMPUTE);
		ProcessDeferredResourceRequests(cmd); // Execute it first thing in the frame here, on main thread, to not allow other thread steal it and execute on different command list!

		CommandList cmd_prepareframe = cmd;
		// remember GraphicsDevice::BeginCommandList does incur some overhead
		//	this is why jobsystem::Execute is used here
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(*camera, cameraPrevious, cameraReflection, cmd);
			UpdateRenderData(visMain, frameCB, cmd);

			barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Image(&rtPrimitiveID_1, rtPrimitiveID_1.desc.layout, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Aliasing(&rtPostprocess, &rtPrimitiveID_1));
			BarrierStackFlush(cmd);

			// These Clear'UAV' must be after the barrier transition to ResourceState::UNORDERED_ACCESS
			device->ClearUAV(&rtMain, 0, cmd);
			device->ClearUAV(&rtPrimitiveID_1, 0, cmd);

			});

		cmd = device->BeginCommandList();
		device->WaitCommandList(cmd, cmd_prepareframe);
		CommandList cmd_slicer = cmd;
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(
				*camera,
				cameraPrevious,
				cameraReflection,
				cmd
			);

			RenderSlicerMeshes(cmd);

			});

		cmd = device->BeginCommandList();
		device->WaitCommandList(cmd, cmd_slicer);
		CommandList cmd_dvr = cmd;
		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {

			BindCameraCB(
				*camera,
				cameraPrevious,
				cameraReflection,
				cmd
			);

			// camera->IsSlicer()
			RenderDirectVolumes(cmd);

			});

		cmd = device->BeginCommandList();
		//device->WaitCommandList(cmd, cmd_dvr);

		jobsystem::Execute(ctx, [this, cmd](jobsystem::JobArgs args) {
			RenderPostprocessChain(cmd);
			});

		// IMPORTANT NOTE:
		//	* this is why JOBSYSTEM is used for rendering process
		//	* ONLY ONCE call of jobsystem::Wait(ctx)
		//	* CommandLists will be submitted sequentially w.r.t. device->BeginCommandList 
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
		gaussianSplattingResources = {};

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

		rtSceneCopy = {};
		rtSceneCopy_tmp = {};
		//rtWaterRipple = {};

		visibilityResources = {};

		rtParticleDistortion_render = {};
		rtParticleDistortion = {};
		
		rtOutlineSource = {};

		distortion_overlay = {};

		return true;
	}
}

namespace vz
{
	GRenderPath3D* NewGRenderPath(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
	{
		return new renderer::GRenderPath3DDetails(swapChain, rtRenderFinal);
	}

	void AddDeferredMIPGen(const Texture& texture, bool preserve_coverage)
	{
		std::lock_guard<std::mutex> lock(renderer::deferredResourceMutex);
		//renderer::deferredResourceLock.lock();
		for (size_t i = 0, n = renderer::deferredMIPGens.size(); i < n; i++)
		{
			auto& it = renderer::deferredMIPGens[i];
			if (it.first.internal_state.get() == texture.internal_state.get())
			{
				return;
			}
		}
		renderer::deferredMIPGens.push_back(std::make_pair(texture, preserve_coverage));
		//renderer::deferredResourceLock.unlock();
	}
	void AddDeferredBlockCompression(const graphics::Texture& texture_src, const graphics::Texture& texture_bc)
	{
		std::lock_guard<std::mutex> lock(renderer::deferredResourceMutex);
		//renderer::deferredResourceLock.lock();
		for (size_t i = 0, n = renderer::deferredBCQueue.size(); i < n; i++)
		{
			auto& it = renderer::deferredBCQueue[i];
			if (it.first.internal_state.get() == texture_src.internal_state.get()
				&& it.second.internal_state.get() == texture_bc.internal_state.get())
			{
				return;
			}
		}
		renderer::deferredBCQueue.push_back(std::make_pair(texture_src, texture_bc));
		//renderer::deferredResourceLock.unlock();
	}
	void AddDeferredTextureCopy(const graphics::Texture& texture_src, const graphics::Texture& texture_dst, const bool mipGen)
	{
		std::lock_guard<std::mutex> lock(renderer::deferredResourceMutex);
		if (!texture_src.IsValid() || !texture_dst.IsValid())
		{
			return;
		}
		//renderer::deferredResourceLock.lock();
		for (size_t i = 0, n = renderer::deferredTextureCopy.size(); i < n; i++)
		{
			auto& it = renderer::deferredTextureCopy[i];
			if (it.first.internal_state.get() == texture_src.internal_state.get()
				&& it.second.internal_state.get() == texture_dst.internal_state.get())
			{
				return;
			}
		}
		renderer::deferredTextureCopy.push_back(std::make_pair(texture_src, texture_dst));
		//renderer::deferredResourceLock.unlock();
	}
	void AddDeferredGeometryGPUBVHUpdate(const Entity entity)
	{
		std::lock_guard<std::mutex> lock(renderer::deferredResourceMutex);
		if (!compfactory::ContainGeometryComponent(entity))
		{
			return;
		}
		for (size_t i = 0, n = renderer::deferredGeometryGPUBVHGens.size(); i < n; i++)
		{
			auto& it = renderer::deferredGeometryGPUBVHGens[i];
			if (it == entity)
			{
				return;
			}
		}
		//renderer::deferredResourceLock.lock();
		renderer::deferredGeometryGPUBVHGens.push_back(entity);
		//renderer::deferredResourceLock.unlock();
	}
	void AddDeferredBufferUpdate(const graphics::GPUBuffer& buffer, const void* data, const uint64_t size, const uint64_t offset)
	{
		std::lock_guard<std::mutex> lock(renderer::deferredResourceMutex);
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
		for (size_t i = 0, n = renderer::deferredBufferUpdate.size(); i < n; i++)
		{
			auto& it = renderer::deferredBufferUpdate[i];
			if (it.first.internal_state.get() == buffer.internal_state.get()
				&& it.second.first == data)
			{
				return;
			}
		}
		//renderer::deferredResourceLock.lock();
		renderer::deferredBufferUpdate.push_back(std::make_pair(buffer, std::make_pair(data_ptr + offset, update_size)));
		//renderer::deferredResourceLock.unlock();
	}
}

