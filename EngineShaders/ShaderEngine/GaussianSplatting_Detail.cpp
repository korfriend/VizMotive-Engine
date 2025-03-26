#include "RenderPath3D_Detail.h"
#include "SortLib.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::CreateGaussianResources(GaussianSplattingResources& res, XMUINT2 resolution)
	{
		res.tileCount = GetGaussianSplattingTileCount(resolution);

		// new version //
		GPUBufferDesc bd;
		bd.usage = Usage::DEFAULT;
		bd.bind_flags = BindFlag::UNORDERED_ACCESS | BindFlag::SHADER_RESOURCE;
		bd.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;

		//bd.stride = sizeof(uint);
		//bd.size = res.tileCount.x * res.tileCount.y * bd.stride * SHADER_ENTITY_TILE_BUCKET_COUNT * 2; // *2: opaque and transparent arrays
		//device->CreateBuffer(&bd, nullptr, &res.touchedTiles_tiledCounts);
		//device->SetName(&res.touchedTiles_tiledCounts, "GaussianSplattingResources::touchedTiles_tiledCounts");
		//
		//device->CreateBuffer(&bd, nullptr, &res.offsetTiles);
		//device->SetName(&res.offsetTiles, "GaussianSplattingResources::offsetTiles");
			
		bd.stride = sizeof(uint);
		bd.size = res.tileCount.x * res.tileCount.y * bd.stride * 2;
		device->CreateBuffer(&bd, nullptr, &res.tileGaussianRange);
		device->SetName(&res.tileGaussianRange, "GaussianSplattingResources::tileGaussianRange");
	}

	void GRenderPath3DDetails::RenderGaussianSplatting(CommandList cmd)
	{
		if (viewMain.visibleRenderables.empty())
			return;

		GSceneDetails* scene_Gdetails = (GSceneDetails*)viewMain.scene->GetGSceneHandle();
		if (scene_Gdetails->renderableComponents_mesh.empty())
		{
			return;
		}

		uint32_t filterMask = GMaterialComponent::FILTER_GAUSSIAN_SPLATTING;

		// Note: the tile_count here must be valid whether the ViewResources was created or not!
		XMUINT2 gsplat_tile_count = XMUINT2(
			(rtMain.desc.width + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE,
			(rtMain.desc.height + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE
		);

		GPUResource unbind;

		static thread_local RenderQueue renderQueue;
		renderQueue.init();
		for (uint32_t instanceIndex : viewMain.visibleRenderables)
		{
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];
			if (!renderable.IsMeshRenderable())
				continue;
			if (!renderable.IsVisibleWith(viewMain.camera->GetVisibleLayerMask()))
				continue;
			if ((renderable.materialFilterFlags & filterMask) == 0)
				continue;

			GGeometryComponent& geometry = *scene_Gdetails->geometryComponents[renderable.geometryIndex];
			if (!geometry.allowGaussianSplatting)
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
		else
		{
			return;
		}

		device->EventBegin("Gaussian Splatting Render", cmd);
		auto range = profiler::BeginRangeGPU("GaussianSplatting Rendering", &cmd);

		BindCommonResources(cmd);

		uint32_t instanceCount = 0;
		// JUST SINGLE DRAWING for MULTIPLE VOLUMES
		const RenderBatch& batch = renderQueue.batches[0];
		for (uint i = 0; i < 1; i++)
		{
			const uint32_t geometry_index = batch.GetGeometryIndex();	// geometry index
			const uint32_t renderable_index = batch.GetRenderableIndex();	// renderable index (base renderable)
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderable_index];
			assert(renderable.IsMeshRenderable());

			GMaterialComponent* material = (GMaterialComponent*)compfactory::GetMaterialComponent(renderable.GetMaterial(0));
			assert(material);

			GGeometryComponent& geometry = *scene_Gdetails->geometryComponents[geometry_index];
			GPrimBuffers* gprim_buffer = geometry.GetGPrimBuffer(0);
			const Primitive* primitive = geometry.GetPrimitive(0);

			UINT width = rtMain.desc.width;
			UINT height = rtMain.desc.height;

			UINT tileWidth = (width + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE;
			UINT tileHeight = (height + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE;


			GGeometryComponent::GaussianSplattingBuffers& gsplat_buffers = gprim_buffer->gaussianSplattingBuffers;
			GaussianPushConstants gsplat_push;
			
			// ------ kickoff -----
			{
				barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianCounterBuffer, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
				BarrierStackFlush(cmd);
			}
			device->ClearUAV(&gsplat_buffers.gaussianCounterBuffer, 0u , cmd);
			// kick off indirect updating
			//device->EventBegin("GaussianSplatting KickOff Update", cmd);
			//device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_KICKOFF], cmd);
			//device->Dispatch(1, 1, 1, cmd);
			//device->EventEnd(cmd);

			uint num_gaussians = primitive->GetNumVertices();
			int threads_per_group = GSPLAT_GROUP_SIZE;
			int num_groups = (num_gaussians + threads_per_group - 1) / threads_per_group; // num_groups

			// ------ preprocess -----
			device->EventBegin("GaussianSplatting - Preprocess", cmd);
			{
				{
					barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));

					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.touchedTiles, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianKernelAttributes, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.offsetTiles, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));

					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianScale_Opacities, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianQuaterinions, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianSHs, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					BarrierStackFlush(cmd);
				}
				device->BindUAV(&rtMain, 10, cmd); // just for debug

				device->BindUAV(&gsplat_buffers.touchedTiles, 0, cmd);
				device->BindUAV(&gsplat_buffers.gaussianKernelAttributes, 1, cmd);
				device->BindUAV(&gsplat_buffers.offsetTiles, 2, cmd);
				device->BindUAV(&gsplat_buffers.gaussianCounterBuffer, 3, cmd);

				device->BindResource(&gsplat_buffers.gaussianScale_Opacities, 0, cmd);
				device->BindResource(&gsplat_buffers.gaussianQuaterinions, 1, cmd);
				device->BindResource(&gsplat_buffers.gaussianSHs, 2, cmd);

				gsplat_push.instanceIndex = batch.instanceIndex;
				gsplat_push.tileWidth = tileWidth;
				gsplat_push.tileHeight = tileHeight;
				gsplat_push.numGaussians = num_gaussians;
				gsplat_push.geometryIndex = gprim_buffer->vbPosW.descriptor_srv;
				gsplat_push.flags |= GSPLAT_FLAG_ANTIALIASING;
				camera->GetIntrinsics(&gsplat_push.focalX, &gsplat_push.focalY, nullptr, nullptr, nullptr);

				// preprocess and calculate touched tiles count
				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_PREPROCESS], cmd);
				device->PushConstants(&gsplat_push, sizeof(GaussianPushConstants), cmd);
				device->Dispatch(
					num_groups,
					1,
					1,
					cmd
				);

				//device->BindUAV(&unbind, 10, cmd);
				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
				device->BindUAV(&unbind, 2, cmd);
				device->BindUAV(&unbind, 3, cmd);
				device->BindResource(&unbind, 0, cmd);
				device->BindResource(&unbind, 1, cmd);
				device->BindResource(&unbind, 2, cmd);
			}
			device->EventEnd(cmd);

			// ----- readback replication count -----
			{
				barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianCounterBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SRC));
				BarrierStackFlush(cmd);
			}
			uint32_t pingplong_readback_index = device->GetBufferIndex();
			device->CopyResource(
				&gsplat_buffers.gaussianCounterBuffer_readback[pingplong_readback_index],
				&gsplat_buffers.gaussianCounterBuffer,
				cmd
			);
			const uint32_t* counter_gsplat = (const uint32_t*)gsplat_buffers.gaussianCounterBuffer_readback[pingplong_readback_index].mapped_data;
			uint32_t num_gaussian_replications = counter_gsplat[0];
			if (num_gaussian_replications == 0)
				break;
			break;
			geometry.UpdateCapacityGaussians(num_gaussian_replications);

			// ------ replication -----
			device->EventBegin("GaussianSplatting - Replication", cmd);
			{
				{
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianCounterBuffer, ResourceState::COPY_SRC, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.offsetTiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					//barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.touchedTiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianKernelAttributes, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));

					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.replicatedGaussianKey, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.replicatedGaussianValue, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					BarrierStackFlush(cmd);
				}

				device->BindUAV(&gsplat_buffers.replicatedGaussianKey, 0, cmd);
				device->BindUAV(&gsplat_buffers.replicatedGaussianValue, 1, cmd);

				device->BindResource(&gsplat_buffers.gaussianKernelAttributes, 0, cmd);
				device->BindResource(&gsplat_buffers.offsetTiles, 1, cmd);
				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_REPLICATE_GAUSSIANS], cmd);

				device->Dispatch(
					num_groups,
					1,
					1,
					cmd
				);

				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
				device->BindResource(&unbind, 0, cmd);
				device->BindResource(&unbind, 1, cmd);
			}
			device->EventEnd(cmd);
			

			// ------ Sort of Gaussian Replications -----
			device->EventBegin("GaussianSplatting - Sort Gaussian Replications", cmd);
			{
				{
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.replicatedGaussianKey, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.sortedIndices, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					BarrierStackFlush(cmd);
				}
				gpusortlib::Sort(num_gaussian_replications, gpusortlib::COMPARISON_UINT64, gsplat_buffers.replicatedGaussianKey, gsplat_buffers.gaussianCounterBuffer, 0,
					gsplat_buffers.sortedIndices, cmd);
			}
			device->EventEnd(cmd);

			// ------ Update Tile Range -----
			device->EventBegin("GaussianSplatting - Update Tile Range", cmd);
			{
				{
					barrierStack.push_back(GPUBarrier::Buffer(&gaussianSplattingResources.tileGaussianRange, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					BarrierStackFlush(cmd);
				}

				device->BindUAV(&gaussianSplattingResources.tileGaussianRange, 0, cmd);

				device->BindResource(&gsplat_buffers.replicatedGaussianKey, 0, cmd);
				device->BindResource(&gsplat_buffers.gaussianCounterBuffer, 1, cmd);

				// preprocess and calculate touched tiles count
				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_IDENTIFY_TILE_RANGES], cmd);
				device->Dispatch(
					num_gaussian_replications / GSPLAT_GROUP_SIZE,
					1,
					1,
					cmd
				);

				device->BindUAV(&unbind, 0, cmd);
				device->BindResource(&unbind, 0, cmd);
				device->BindResource(&unbind, 1, cmd);
			}

			device->EventEnd(cmd);

			// ------ Blending Gaussians: Final Renderout -----
			device->EventBegin("GaussianSplatting - Blending Replicated Gaussian", cmd);
			{
				{
					barrierStack.push_back(GPUBarrier::Buffer(&gaussianSplattingResources.tileGaussianRange, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.sortedIndices, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					BarrierStackFlush(cmd);
				}

				device->BindUAV(&rtMain, 0, cmd);

				device->BindResource(&gsplat_buffers.gaussianKernelAttributes, 0, cmd);
				device->BindResource(&gaussianSplattingResources.tileGaussianRange, 1, cmd);
				device->BindResource(&gsplat_buffers.replicatedGaussianValue, 2, cmd);
				device->BindResource(&gsplat_buffers.sortedIndices, 3, cmd);

				// preprocess and calculate touched tiles count
				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_BLEND_GAUSSIAN], cmd);
				device->Dispatch(
					tileWidth,
					tileHeight,
					1,
					cmd
				);

				device->BindUAV(&unbind, 0, cmd);
				device->BindResource(&unbind, 0, cmd);
				device->BindResource(&unbind, 1, cmd);
				device->BindResource(&unbind, 2, cmd);
				device->BindResource(&unbind, 3, cmd);
			}

			device->EventEnd(cmd);
		}

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
}