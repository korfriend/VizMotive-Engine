#include "RenderPath3D_Details.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::RenderSlicerMeshes(CommandList cmd)
	{
		if (viewMain.visibleRenderables.empty())
			return;

		GSceneDetails* scene_Gdetails = (GSceneDetails*)viewMain.scene->GetGSceneHandle();
		if (scene_Gdetails->renderableComponents_mesh.empty())
		{
			return;
		}

		enum SLICER_TEX {
			RENDER_OUT = 0,
			LAYER_0_COLOR_PACKED,
			LAYER_1_COLOR_PACKED,
			LAYER_0_DEPTH,
			LAYER_1_DEPTH,
			LAYER_0_THICK_ASUM_PACKED,
			LAYER_1_THICK_ASUM_PACKED,
		};
		graphics::Texture slicer_textures[] = {
			rtMain,				// inout_color, desc.layout
			rtPrimitiveID_1,	// layer0_color, desc.layout
			rtPrimitiveID_2,	// layer1_color, desc.layout
			depthBufferMain,	// layer0_depth, desc.layout (FLOAT)
			rtLinearDepth,		// layer1_depth, desc.layout (FLOAT)
			depthBuffer_Copy,	// layer0_thick_asum, desc.layout (UINT)
			depthBuffer_Copy1,	// layer1_thick_asum, desc.layout (UINT)
		};
		for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
		{
			vzlog_assert(slicer_textures[i].IsValid(), "Texture Resources must be Valid!");
			if (!slicer_textures[i].IsValid())
			{
				return;
			}
		}

		device->EventBegin("Slicer Mesh Render", cmd);
		auto range = profiler::BeginRangeGPU("Slicer Rendering", &cmd);

		BindCommonResources(cmd);

		uint32_t filterMask = GMaterialComponent::FILTER_OPAQUE | GMaterialComponent::FILTER_TRANSPARENT;

		// Note: the tile_count here must be valid whether the ViewResources was created or not!
		XMUINT2 tile_count = GetViewTileCount(XMUINT2(rtMain.desc.width, rtMain.desc.height));

		GPUResource unbind;

		// NOTE: "static thread_local" technique!!!
		//	the parameter must be initialized after the "static thread_local" declaration!
		//	
		static thread_local RenderQueue renderQueue;
		renderQueue.init();
		for (uint32_t instanceIndex : viewMain.visibleRenderables)
		{
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];
			if (!renderable.IsMeshRenderable())
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
			renderQueue.sort_opaque(); // F2B
		}

		struct InstancedBatch
		{
			uint32_t geometryIndex = ~0u;	// geometryIndex
			uint32_t renderableIndex = ~0u;
			std::vector<uint32_t> materialIndices;
			bool forceAlphatestForDithering = false;
			AABB aabb;
			uint32_t lod = 0;
		} instancedBatch = {};

		SlicerComponent* slicer = (SlicerComponent*)this->camera;
		assert(slicer->GetComponentType() == ComponentType::SLICER);

		SlicerMeshPushConstants push;
		push.sliceThickness = slicer->GetThickness();
		// compute pixelSpace
		{
			auto unproj = [&](float screenX, float screenY, float screenZ,
				float viewportX, float viewportY, float viewportWidth, float viewportHeight,
				const XMMATRIX& invViewProj)
				{
					float ndcX = ((screenX - viewportX) / viewportWidth) * 2.0f - 1.0f;
					float ndcY = 1.0f - ((screenY - viewportY) / viewportHeight) * 2.0f; // y는 반전
					float ndcZ = screenZ; // 보통 0~1 범위로 제공됨

					XMVECTOR ndcPos = XMVectorSet(ndcX, ndcY, ndcZ, 1.0f);

					XMVECTOR worldPos = XMVector4Transform(ndcPos, invViewProj);

					worldPos = XMVectorScale(worldPos, 1.0f / XMVectorGetW(worldPos));

					return worldPos;
				};
			
			XMMATRIX inv_vp = XMLoadFloat4x4(&slicer->GetInvViewProjection());
			XMVECTOR world_pos0 = unproj(0.0f, 0.0f, 0.0f, viewport.top_left_x, viewport.top_left_y, viewport.width, viewport.height, inv_vp);
			XMVECTOR world_pos1 = unproj(1.0f, 0.0f, 0.0f, viewport.top_left_x, viewport.top_left_y, viewport.width, viewport.height, inv_vp);

			XMVECTOR diff = XMVectorSubtract(world_pos0, world_pos1);
			push.pixelSize = XMVectorGetX(XMVector3Length(diff));
		}


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
				assert(parts.size() == instancedBatch.materialIndices.size());
				for (uint32_t part_index = 0, num_parts = parts.size(); part_index < num_parts; ++part_index)
				{
					const Primitive& part = parts[part_index];
					GPrimBuffers& part_buffer = *(GPrimBuffers*)geometry.GetGPrimBuffer(part_index);

					uint32_t material_index = instancedBatch.materialIndices[part_index];
					const GMaterialComponent& material = *scene_Gdetails->materialComponents[material_index];

					bool is_renderable = filterMask & material.GetFilterMaskFlags();
					if (!is_renderable || !part_buffer.bvhBuffers.primitiveCounterBuffer.IsValid())
					{
						continue;
					}

					device->BindComputeShader(&shaders[CSTYPE_MESH_SLICER], cmd);

					for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
					{
						graphics::Texture& texture = slicer_textures[i];
						device->BindUAV(&texture, i, cmd);
						barrierStack.push_back(GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS));
					}
					BarrierStackFlush(cmd);

					push.instanceIndex = instancedBatch.renderableIndex;
					push.materialIndex = material_index;
					push.BVH_counter = device->GetDescriptorIndex(&part_buffer.bvhBuffers.primitiveCounterBuffer, SubresourceType::SRV);
					push.BVH_nodes = device->GetDescriptorIndex(&part_buffer.bvhBuffers.bvhNodeBuffer, SubresourceType::SRV);
					push.BVH_primitives = device->GetDescriptorIndex(&part_buffer.bvhBuffers.primitiveBuffer, SubresourceType::SRV);
					
					renderable.IsSlicerSolidFill() ? 
						push.sliceFlags &= ~SLICER_FLAG_ONLY_OUTLINE : push.sliceFlags |= SLICER_FLAG_ONLY_OUTLINE;
					push.outlineThickness = slicer->GetOutlineThickness() <= 0? renderable.GetOutlineThickness() : slicer->GetOutlineThickness();
					
					device->PushConstants(&push, sizeof(push), cmd);

					device->Dispatch(
						tile_count.x,
						tile_count.y,
						1,
						cmd
					);

					for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
					{
						device->BindUAV(&unbind, i, cmd);
						graphics::Texture& texture = slicer_textures[i];
						barrierStack.push_back(GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout));
					}
					BarrierStackFlush(cmd);

					if (push.sliceThickness == 0.f || (push.sliceFlags & SLICER_FLAG_ONLY_OUTLINE)) {
						//device->Barrier(GPUBarrier::Memory(), cmd);
						barrierStack.push_back(GPUBarrier::Image(&slicer_textures[RENDER_OUT], slicer_textures[RENDER_OUT].desc.layout, ResourceState::UNORDERED_ACCESS));
						barrierStack.push_back(GPUBarrier::Image(&slicer_textures[LAYER_1_THICK_ASUM_PACKED], slicer_textures[LAYER_1_THICK_ASUM_PACKED].desc.layout, ResourceState::UNORDERED_ACCESS));
						barrierStack.push_back(GPUBarrier::Image(&slicer_textures[LAYER_1_DEPTH], slicer_textures[LAYER_1_DEPTH].desc.layout, ResourceState::SHADER_RESOURCE_COMPUTE));
						BarrierStackFlush(cmd);
						device->BindComputeShader(&shaders[CSTYPE_SLICER_OUTLINE], cmd);

						device->BindUAV(&slicer_textures[RENDER_OUT], 0, cmd);
						device->BindUAV(&slicer_textures[LAYER_1_THICK_ASUM_PACKED], 1, cmd);
						device->BindResource(&slicer_textures[LAYER_1_DEPTH], 0, cmd);

						device->Dispatch(
							tile_count.x,
							tile_count.y,
							1,
							cmd
						);

						barrierStack.push_back(GPUBarrier::Image(&slicer_textures[RENDER_OUT], ResourceState::UNORDERED_ACCESS, slicer_textures[RENDER_OUT].desc.layout));
						barrierStack.push_back(GPUBarrier::Image(&slicer_textures[LAYER_1_THICK_ASUM_PACKED], ResourceState::UNORDERED_ACCESS, slicer_textures[LAYER_1_THICK_ASUM_PACKED].desc.layout));
						barrierStack.push_back(GPUBarrier::Image(&slicer_textures[LAYER_1_DEPTH], ResourceState::SHADER_RESOURCE_COMPUTE, slicer_textures[LAYER_1_DEPTH].desc.layout));
						BarrierStackFlush(cmd);

						device->BindUAV(&unbind, 0, cmd);
						device->BindUAV(&unbind, 1, cmd);
						device->BindResource(&unbind, 0, cmd);
					}

				}
			};


		auto range_1 = profiler::BeginRangeGPU("Slicer Ray-Processing", &cmd);
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
		}
		BatchDrawingFlush();
		profiler::EndRange(range_1);

		if (push.sliceThickness > 0) 
		{
			auto range_2 = profiler::BeginRangeGPU("Slicer Resolve", &cmd);
			device->BindComputeShader(&shaders[CSTYPE_KBUFFER_2_RESOLVE], cmd);

			for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
			{
				graphics::Texture& texture = slicer_textures[i];
				device->BindUAV(&texture, i, cmd);
				barrierStack.push_back(GPUBarrier::Image(&texture, texture.desc.layout, ResourceState::UNORDERED_ACCESS));
			}
			BarrierStackFlush(cmd);

			device->Dispatch(
				tile_count.x,
				tile_count.y,
				1,
				cmd
			);

			for (size_t i = 0, n = sizeof(slicer_textures) / sizeof(graphics::Texture); i < n; ++i)
			{
				device->BindUAV(&unbind, i, cmd);
				graphics::Texture& texture = slicer_textures[i];
				barrierStack.push_back(GPUBarrier::Image(&texture, ResourceState::UNORDERED_ACCESS, texture.desc.layout));
			}
			BarrierStackFlush(cmd);
			profiler::EndRange(range_2);
		}

		profiler::EndRange(range);
		device->EventEnd(cmd);
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
		auto range = profiler::BeginRangeGPU("Direct Volume Rendering", &cmd);

		BindCommonResources(cmd);

		uint32_t filterMask = GMaterialComponent::FILTER_VOLUME;

		// Note: the tile_count here must be valid whether the ViewResources was created or not!
		XMUINT2 tile_count = GetViewTileCount(XMUINT2(rtMain.desc.width, rtMain.desc.height));

		GPUResource unbind;

		// NOTE: "static thread_local" technique!!!
		//	the parameter must be initialized after the "static thread_local" declaration!
		//	
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
			renderQueue.sort_opaque(); // F2B
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
				volume_push.mask_value_range = 255.f;
				volume_push.mask_unormid_otf_map = volume_push.mask_value_range / (otf->GetHeight() > 1 ? otf->GetHeight() - 1 : 1.f);

				volume_push.inout_color_Index = device->GetDescriptorIndex(&rtMain, SubresourceType::UAV);
				volume_push.inout_linear_depth_Index = device->GetDescriptorIndex(&rtLinearDepth, SubresourceType::UAV);
			}

			barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, rtLinearDepth.desc.layout, ResourceState::UNORDERED_ACCESS));
			BarrierStackFlush(cmd);

			device->BindComputeShader(&shaders[CSTYPE_DVR_DEFAULT], cmd);
			device->PushConstants(&volume_push, sizeof(VolumePushConstants), cmd);

			device->Dispatch(
				tile_count.x,
				tile_count.y,
				1,
				cmd
			);

			barrierStack.push_back(GPUBarrier::Image(&rtMain, ResourceState::UNORDERED_ACCESS, rtMain.desc.layout));
			barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, ResourceState::UNORDERED_ACCESS, rtLinearDepth.desc.layout));
			BarrierStackFlush(cmd);

			break; // TODO: at this moment, just a single volume is supported!
		}

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
}