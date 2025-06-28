#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::RenderSlicerMeshes(CommandList cmd)
	{
		if (visMain.visibleRenderables_Mesh.empty())
			return;

		uint32_t filterMask = GMaterialComponent::FILTER_OPAQUE | GMaterialComponent::FILTER_TRANSPARENT;

		// Note: the tile_count here must be valid whether the ViewResources was created or not!
		XMUINT2 tile_count = GetVisibilityTileCount(XMUINT2(rtMain.desc.width, rtMain.desc.height));

		GPUResource unbind;

		// NOTE: "static thread_local" technique!!!
		//	the parameter must be initialized after the "static thread_local" declaration!
		//	
		static thread_local RenderQueue renderQueue;
		renderQueue.init();
		for (uint32_t instanceIndex : visMain.visibleRenderables_Mesh)
		{
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];
			if (!renderable.layeredmask->IsVisibleWith(visMain.layeredmask->GetVisibleLayerMask()))
				continue;
			if ((renderable.materialFilterFlags & filterMask) == 0)
				continue;

			const float distance = math::Distance(visMain.camera->GetWorldEye(), renderable.GetAABB().getCenter());
			if (distance > renderable.GetFadeDistance() + renderable.GetAABB().getRadius())
				continue;

			renderQueue.add(renderable.geometry->geometryIndex, instanceIndex, distance, renderable.sortBits);
		}

		graphics::Texture slicer_textures[] = {
			rtMain,				// inout_color, ResourceState::UNORDERED_ACCESS
			rtPrimitiveID_1,	// counter (8bit) / mask (8bit) / intermediate distance map (16bit), ResourceState::UNORDERED_ACCESS
			rtPrimitiveID_2,	// R32G32B32A32_UINT - Layer_Packed0, ResourceState::UNORDERED_ACCESS
			rtLinearDepth,		// R32G32_UINT - Layer_Packed1, ResourceState::UNORDERED_ACCESS
		};

		if (!renderQueue.empty())
		{
			// We use a policy where the closer it is to the front, the higher the priority.
			renderQueue.sort_opaque(); // F2B
		}
		else
		{
			for (size_t i = 0, n = 2; i < n; ++i)
			{
				graphics::Texture& texture = slicer_textures[i];
				barrierStack.push_back(GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout));
			}
			BarrierStackFlush(cmd);
			return;
		}


		for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
		{
			vzlog_assert(slicer_textures[i].IsValid(), "RWTexture Resources must be Valid!");
			if (!slicer_textures[i].IsValid())
			{
				return;
			}
		}

		device->EventBegin("Slicer Mesh Render", cmd);

		// NOTE: rtMain and rtPrimitiveID_1 are already UAV state
		for (size_t i = 2, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
		{
			graphics::Texture& texture = slicer_textures[i];
			barrierStack.push_back(GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS));
		}
		BarrierStackFlush(cmd);

		BindCommonResources(cmd);

		InstancedBatch instancedBatch = {};

		GSlicerComponent* slicer = (GSlicerComponent*)this->camera;
		assert(slicer->GetComponentType() == ComponentType::SLICER);

		const float slicer_thickness = slicer->GetThickness();

		SlicerMeshPushConstants push;

		// This will be called every time we start a new draw call:
		//	calls draw per a geometry part
		auto BatchDrawingFlush = [&]()
			{
				if (instancedBatch.geometryIndex == ~0u)
					return;

				GGeometryComponent& geometry = *scene_Gdetails->geometryComponents[instancedBatch.geometryIndex];

				if (!geometry.HasRenderData() || !geometry.HasBVH())
					return;

				GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instancedBatch.renderableIndex];

				// Note: geometries and materials are scanned resources from the scene.	

				const std::vector<Primitive>& parts = geometry.GetPrimitives();
				assert(parts.size() == renderable.materials.size());
				for (uint32_t part_index = 0, num_parts = parts.size(); part_index < num_parts; ++part_index)
				{
					const Primitive& part = parts[part_index];
					if (!part.HasRenderData())
					{
						continue;
					}
					GPrimBuffers& part_buffer = *(GPrimBuffers*)geometry.GetGPrimBuffer(part_index);

					const GMaterialComponent& material = *renderable.materials[part_index];

					bool is_renderable = filterMask & material.GetFilterMaskFlags();
					if (!is_renderable || !part_buffer.bvhBuffers.primitiveCounterBuffer.IsValid())
					{
						continue;
					}

					push.instanceIndex = instancedBatch.renderableIndex;
					push.materialIndex = material.materialIndex;
					push.BVH_counter = device->GetDescriptorIndex(&part_buffer.bvhBuffers.primitiveCounterBuffer, SubresourceType::SRV);
					push.BVH_nodes = device->GetDescriptorIndex(&part_buffer.bvhBuffers.bvhNodeBuffer, SubresourceType::SRV);
					push.BVH_primitives = device->GetDescriptorIndex(&part_buffer.bvhBuffers.primitiveBuffer, SubresourceType::SRV);
					
					renderable.IsSlicerSolidFill() ? 
						push.sliceFlags &= ~SLICER_FLAG_ONLY_OUTLINE : push.sliceFlags |= SLICER_FLAG_ONLY_OUTLINE;

					push.outlineThickness = slicer->GetOutlineThickness() <= 0? renderable.GetOutlineThickness() : slicer->GetOutlineThickness();

					device->BindComputeShader(
						&shaders[slicer->IsCurvedSlicer()? CSTYPE_MESH_CURVED_SLICER : CSTYPE_MESH_SLICER], cmd);

					for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
					{
						device->BindUAV(&slicer_textures[i], i, cmd);
					}

					device->PushConstants(&push, sizeof(push), cmd);

					device->Dispatch(
						tile_count.x,
						tile_count.y,
						1,
						cmd
					);

					if (slicer_thickness == 0.f || (push.sliceFlags & SLICER_FLAG_ONLY_OUTLINE)) {

						device->BindComputeShader(&shaders[CSTYPE_SLICER_OUTLINE], cmd);

						{
							for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
								barrierStack.push_back(GPUBarrier::Memory(&slicer_textures[i]));
							BarrierStackFlush(cmd);
						}
						device->Dispatch(
							tile_count.x,
							tile_count.y,
							1,
							cmd
						);
					}

					{
						for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
							barrierStack.push_back(GPUBarrier::Memory(&slicer_textures[i]));
						BarrierStackFlush(cmd);
					}
				}
			};

		auto range_1 = profiler::BeginRangeGPU("Slicer Ray-Processing", &cmd);
		// The following loop is writing the instancing batches to a GPUBuffer:
		//	RenderQueue is sorted based on mesh index, so when a new mesh or stencil request is encountered, we need to flush the batch
		//	Imagine a scenario:
		//		* tens of sphere-shaped renderables (actors) that have the same sphere geoemtry
		//		* multiple draw calls of the renderables vs. a single drawing of multiple instances (composed of spheres)
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
				//instancedBatch.userStencilRefOverride = userStencilRefOverride;
				instancedBatch.geometryIndex = geometry_index;
				instancedBatch.renderableIndex = renderable_index;
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
		}
		BatchDrawingFlush();
		profiler::EndRange(range_1);

		if (slicer_thickness > 0)
		{
			auto range_2 = profiler::BeginRangeGPU("Slicer Resolve", &cmd);
			device->BindComputeShader(&shaders[CSTYPE_SLICE_RESOLVE_KB2], cmd);

			device->Dispatch(
				tile_count.x,
				tile_count.y,
				1,
				cmd
			);

			profiler::EndRange(range_2);
		}

		for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
		{
			device->BindUAV(&unbind, i, cmd);
		}

		for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
		{
			graphics::Texture& texture = slicer_textures[i];
			barrierStack.push_back(GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout));
		}
		BarrierStackFlush(cmd);

		device->EventEnd(cmd);
	}


	SHADERTYPE selectVolumeShader(CameraComponent* camera, VolumePushConstants& push)
	{
		SHADERTYPE shader_type = CSTYPE_DVR_WoKB;
		
		if (camera->IsSlicer())
		{
			SlicerComponent* slicer = (SlicerComponent*)camera;
			if (slicer->GetThickness() == 0)
			{
				shader_type = camera->IsCurvedSlicer() ? CSTYPE_DVR_SLICER_CURVED_NOTHICKNESS : CSTYPE_DVR_SLICER_NOTHICKNESS;
			}
			else
			{
				if (camera->IsCurvedSlicer())
				{
					shader_type = (SHADERTYPE)((uint32_t)CSTYPE_DVR_SLICER_CURVED_2KB + (uint32_t)camera->GetDVRType());
				}
				else
				{
					shader_type = (SHADERTYPE)((uint32_t)CSTYPE_DVR_SLICER_2KB + (uint32_t)camera->GetDVRType());
				}
			}
		}
		else
		{
			shader_type = (SHADERTYPE)((uint32_t)CSTYPE_DVR_WoKB + (uint32_t)camera->GetDVRType());
		}

		return shader_type;
	}

	void GRenderPath3DDetails::RenderDirectVolumes(CommandList cmd)
	{
		if (visMain.visibleRenderables_Volume.empty())
			return;

		uint32_t filterMask = GMaterialComponent::FILTER_VOLUME;

		// Note: the tile_count here must be valid whether the ViewResources was created or not!
		XMUINT2 tile_count = GetVisibilityTileCount(XMUINT2(rtMain.desc.width, rtMain.desc.height));

		GPUResource unbind;

		// NOTE: "static thread_local" technique!!!
		//	the parameter must be initialized after the "static thread_local" declaration!
		//	
		static thread_local RenderQueue renderQueue;
		renderQueue.init();
		for (uint32_t instanceIndex : visMain.visibleRenderables_Volume)
		{
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];
			if (renderable.GetRenderableType() != RenderableType::VOLUME_RENDERABLE)
				continue;
			LayeredMaskComponent* layeredmask = visMain.camera->GetLayeredMaskComponent();
			if (!renderable.layeredmask->IsVisibleWith(layeredmask->GetVisibleLayerMask()))
				continue;
			if ((renderable.materialFilterFlags & filterMask) == 0)
				continue;

			const float distance = math::Distance(visMain.camera->GetWorldEye(), renderable.GetAABB().getCenter());
			if (distance > renderable.GetFadeDistance() + renderable.GetAABB().getRadius())
				continue;

			renderQueue.add(~0u, instanceIndex, distance, renderable.sortBits);
		}
		if (!renderQueue.empty())
		{
			// We use a policy where the closer it is to the front, the higher the priority.
			renderQueue.sort_opaque(); // F2B
		}
		else
		{
			return;
		}

		device->EventBegin("Direct Volume Render", cmd);
		auto range = profiler::BeginRangeGPU("Direct Volume Rendering", &cmd);

		BindCommonResources(cmd);

		// TODO 
		// JUST SINGLE DRAWING for MULTIPLE VOLUMES
		const RenderBatch& batch = renderQueue.batches[0];
		{
			const uint32_t geometry_index = batch.GetGeometryIndex();	// geometry index
			const uint32_t renderable_index = batch.GetRenderableIndex();	// renderable index (base renderable)
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderable_index];
			assert(renderable.GetRenderableType() == RenderableType::VOLUME_RENDERABLE);

			GMaterialComponent* material = renderable.materials[0];
			assert(material);

			GVolumeComponent* volume = material->volumeTextures[SCU32(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP)];
			assert(volume);
			assert(volume->IsValidVolume());

			MaterialComponent::LookupTableSlot target_lookup_slot = camera->GetDVRLookupSlot();
			GTextureComponent* otf = material->textureLookups[SCU32(target_lookup_slot)];
			assert(otf);
			Entity entity_otf = otf->GetEntity();
			XMFLOAT2 tableValidBeginEndRatioX = otf->GetTableValidBeginEndRatioX();

			const GPUBuffer& bitmask_buffer = volume->GetVisibleBitmaskBuffer(entity_otf);
			int bitmaskbuffer = device->GetDescriptorIndex(&bitmask_buffer, SubresourceType::SRV);

			VolumePushConstants volume_push;
			{
				const XMFLOAT3& vox_size = volume->GetVoxelSize();
				volume_push.instanceIndex = batch.instanceIndex;
				volume_push.sculptStep = -1;
				volume_push.opacity_correction = 1.f;
				volume_push.main_visible_min_sample = tableValidBeginEndRatioX.x;
				volume_push.target_otf_slot = SCU32(target_lookup_slot);
				volume_push.bitmaskbuffer = bitmaskbuffer;

				//volume_push.mask_unormid_otf_map = volume_push.mask_value_range / (otf->GetHeight() > 1 ? otf->GetHeight() - 1 : 1.f);

				volume_push.inout_color_Index = device->GetDescriptorIndex(&rtMain, SubresourceType::UAV);
				volume_push.inout_linear_depth_Index = device->GetDescriptorIndex(&rtLinearDepth, SubresourceType::UAV);
			}

			barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));
			if (camera->IsSlicer())
			{
				// note that rtPrimitiveID_1.desc.layout is ResourceState::SHADER_RESOURCE_COMPUTE
				//  so, the following barriers causes unintended barrier transition warning (breakdown when GPU debugging layer is activated)
				//barrierStack.push_back(GPUBarrier::Image(&rtPrimitiveID_1, rtPrimitiveID_1.desc.layout, ResourceState::SHADER_RESOURCE_COMPUTE));
				//barrierStack.push_back(GPUBarrier::Image(&rtPrimitiveID_2, rtPrimitiveID_2.desc.layout, ResourceState::SHADER_RESOURCE_COMPUTE));
				//barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, rtLinearDepth.desc.layout, ResourceState::SHADER_RESOURCE_COMPUTE));

				device->BindResource(&rtPrimitiveID_1, 0, cmd);
				device->BindResource(&rtPrimitiveID_2, 1, cmd);
				device->BindResource(&rtLinearDepth, 2, cmd);
			}
			else
			{
				barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, rtLinearDepth.desc.layout, ResourceState::UNORDERED_ACCESS));
			}
			BarrierStackFlush(cmd);

			device->BindComputeShader(&shaders[selectVolumeShader(camera, volume_push)], cmd);
			device->PushConstants(&volume_push, sizeof(VolumePushConstants), cmd);

			device->Dispatch(
				tile_count.x,
				tile_count.y,
				1,
				cmd
			);

			barrierStack.push_back(GPUBarrier::Image(&rtMain, ResourceState::UNORDERED_ACCESS, rtMain.desc.layout));

			if (camera->IsSlicer())
			{
				//barrierStack.push_back(GPUBarrier::Image(&rtPrimitiveID_1, ResourceState::SHADER_RESOURCE_COMPUTE, rtPrimitiveID_1.desc.layout));
				//barrierStack.push_back(GPUBarrier::Image(&rtPrimitiveID_2, ResourceState::SHADER_RESOURCE_COMPUTE, rtPrimitiveID_2.desc.layout));
				//barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, ResourceState::SHADER_RESOURCE_COMPUTE, rtLinearDepth.desc.layout));

				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
				device->BindUAV(&unbind, 2, cmd);
			}
			else
			{
				barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, ResourceState::UNORDERED_ACCESS, rtLinearDepth.desc.layout));
			}
			BarrierStackFlush(cmd);
		}

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
}