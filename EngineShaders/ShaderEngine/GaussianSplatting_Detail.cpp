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

			UINT tileX = (width + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE;
			UINT tileY = (height + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE;


			GGeometryComponent::GaussianSplattingBuffers& gsplat_buffers = gprim_buffer->gaussianSplattingBuffers;
			GaussianPushConstants gsplat_push;
			
			// ------ kickoff -----
			{
				barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianCounterBuffer, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
				BarrierStackFlush(cmd);
			}
			device->BindUAV(&gsplat_buffers.gaussianCounterBuffer, 10, cmd);
			device->ClearUAV(&gsplat_buffers.gaussianCounterBuffer, 0u , cmd);
			// kick off indirect updating
			//device->EventBegin("GaussianSplatting KickOff Update", cmd);
			//device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_KICKOFF], cmd);
			//device->Dispatch(1, 1, 1, cmd);
			//device->EventEnd(cmd);

			uint num_gaussians = primitive->GetNumVertices();
			int threads_per_group = 256;
			int num_groups = (num_gaussians + threads_per_group - 1) / threads_per_group; // num_groups

			// ------ preprocess -----
			device->EventBegin("GaussianSplatting Preprocess", cmd);
			{
				{
					barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));

					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.touchedTiles, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianKernelAttributes, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.offsetTiles, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));

					//barrierStack.push_back(GPUBarrier::Buffer(&gprim_buffer->generalBuffer, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianScale_Opacities, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianQuaterinions, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.gaussianSHs, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					BarrierStackFlush(cmd);
				}
				device->BindUAV(&rtMain, 0, cmd); // just for debug

				device->BindUAV(&gsplat_buffers.touchedTiles, 1, cmd);
				device->BindUAV(&gsplat_buffers.gaussianKernelAttributes, 2, cmd);
				device->BindUAV(&gsplat_buffers.offsetTiles, 3, cmd);

				device->BindResource(&gsplat_buffers.gaussianScale_Opacities, 0, cmd);
				device->BindResource(&gsplat_buffers.gaussianQuaterinions, 1, cmd);
				device->BindResource(&gsplat_buffers.gaussianSHs, 2, cmd);

				gsplat_push.instanceIndex = batch.instanceIndex;
				gsplat_push.tileX = tileX;
				gsplat_push.numGaussians = num_gaussians;
				gsplat_push.geometryIndex = gprim_buffer->vbPosW.descriptor_srv;

				// preprocess and calculate touched tiles count
				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_PREPROCESS], cmd);
				device->PushConstants(&gsplat_push, sizeof(GaussianPushConstants), cmd);
				device->Dispatch(
					num_groups,
					1,
					1,
					cmd
				);

				//device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
				device->BindUAV(&unbind, 2, cmd);
				device->BindUAV(&unbind, 3, cmd);
				device->BindResource(&unbind, 0, cmd);
				device->BindResource(&unbind, 1, cmd);
				device->BindResource(&unbind, 2, cmd);
			}
			device->EventEnd(cmd);

			uint32_t num_gaussian_replications = gsplat_buffers.capacityGaussians;
			// ------ replication -----
			device->EventBegin("GaussianSplatting Replication", cmd);
			{
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

				const uint32_t* counter_gsplat = (const uint32_t*)gsplat_buffers.gaussianCounterBuffer_readback[(pingplong_readback_index + 1) % 2].mapped_data;
				num_gaussian_replications = counter_gsplat[0];
				geometry.UpdateCapacityGaussians(num_gaussian_replications);

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
			device->EventBegin("BVH - Sort Gaussian Replications", cmd);
			{
				{
					barrierStack.push_back(GPUBarrier::Buffer(&gsplat_buffers.replicatedGaussianKey, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					BarrierStackFlush(cmd);
				}
				gpusortlib::Sort(num_gaussian_replications, gpusortlib::COMPARISON_UINT64, gsplat_buffers.replicatedGaussianKey, gsplat_buffers.gaussianCounterBuffer, 0,
					gsplat_buffers.sortedIndices, cmd);
			}
			device->EventEnd(cmd);

			/*
			// even -> srcBuffer is offsetTilesPong
			GPUBuffer* srcBuffer = ((iters % 2) == 0) ? &gs_buffers.offsetTilesPong : &gs_buffers.offsetTilesPing;

			{
				GPUBarrier barriers[] =
				{
					// must check ResourceState here
					GPUBarrier::Buffer(srcBuffer, ResourceState::SHADER_RESOURCE, ResourceState::COPY_SRC),
					GPUBarrier::Buffer(&gs_buffers.totalSumBufferHost, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_DST)
				};
				device->Barrier(barriers, _countof(barriers), cmd);
			}

			device->CopyBuffer(
				&gs_buffers.totalSumBufferHost,		// dst buffer
				0,									// dst offset 
				srcBuffer,							// selected src buffer (ping or pong buffer)
				((numGaussians - 1) * sizeof(UINT)),// src offset
				sizeof(UINT),						// copy size
				cmd
			);

			{
				GPUBarrier barriers2[] =
				{
					GPUBarrier::Buffer(srcBuffer, ResourceState::COPY_SRC, ResourceState::SHADER_RESOURCE),
					GPUBarrier::Buffer(&gs_buffers.totalSumBufferHost, ResourceState::COPY_DST, ResourceState::UNORDERED_ACCESS)
				};
				device->Barrier(barriers2, _countof(barriers2), cmd);
			}

			// duplicate with keys
			// check readback buffer and numInstance(push constant)
			device->BindUAV(&gs_buffers.sortKBufferEven, 0, cmd);
			device->BindUAV(&gs_buffers.sortVBufferEven, 1, cmd);
			device->BindResource(&gs_buffers.gaussianKernelAttributes, 0, cmd);
			device->BindResource(srcBuffer, 1, cmd);

			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.sortKBufferEven, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.sortVBufferEven, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));

			BarrierStackFlush(cmd);

			device->BindComputeShader(&shaders[CSTYPE_GS_DUPLICATED_GAUSSIANS], cmd);
			device->PushConstants(&gaussian_sort, sizeof(GaussianSortConstants), cmd);
			device->Dispatch(
				numGroups,
				1,
				1,
				cmd
			);

			device->BindUAV(&unbind, 0, cmd);
			device->BindUAV(&unbind, 1, cmd);
			device->BindResource(&unbind, 0, cmd);
			device->BindResource(&unbind, 1, cmd);

			//barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.sortKBufferEven, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
			//barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.sortVBufferEven, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));

			// radix sort test

			UINT numRadixSortBlocksPerWorkgroup = 32;
			UINT sortBufferSizeMultiplier = 4;

			UINT globalInvocationSize = numGaussians * sortBufferSizeMultiplier / numRadixSortBlocksPerWorkgroup;
			UINT remainder = numGaussians * sortBufferSizeMultiplier % numRadixSortBlocksPerWorkgroup;

			globalInvocationSize += remainder > 0 ? 1 : 0;
			auto numWorkgroups = (globalInvocationSize + 256 - 1) / 256;
			auto numInstances = numGaussians * sortBufferSizeMultiplier;

			for (auto i = 0; i < 8; ++i) {
				// Hist pass
				device->BindComputeShader(&shaders[CSTYPE_GS_RADIX_HIST_GAUSSIANS], cmd);

				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);

				if (i % 2 == 0) {
					device->BindUAV(&gs_buffers.sortKBufferEven, 0, cmd);
					device->BindUAV(&gs_buffers.sortHistBuffer, 1, cmd);
				}
				else {
					device->BindUAV(&gs_buffers.sortKBufferOdd, 0, cmd);
					device->BindUAV(&gs_buffers.sortHistBuffer, 1, cmd);
				}

				auto invocationSize = (numInstances + numRadixSortBlocksPerWorkgroup - 1) / numRadixSortBlocksPerWorkgroup;
				invocationSize = (invocationSize + 255) / 256;

				gaussian_radix.g_num_elements = numInstances;
				gaussian_radix.g_num_blocks_per_workgroup = numRadixSortBlocksPerWorkgroup;
				gaussian_radix.g_shift = i * 8;
				gaussian_radix.g_num_workgroups = invocationSize;

				device->PushConstants(&gaussian_radix, sizeof(GaussianRadixConstants), cmd);
				device->Dispatch(invocationSize, 1, 1, cmd);
				BarrierStackFlush(cmd);

				// Sort pass
				device->BindComputeShader(&shaders[CSTYPE_GS_RADIX_SORT_GAUSSIANS], cmd);

				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
				device->BindUAV(&unbind, 2, cmd);
				device->BindUAV(&unbind, 3, cmd);

				if (i % 2 == 0) {
					device->BindUAV(&gs_buffers.sortKBufferEven, 0, cmd);
					device->BindUAV(&gs_buffers.sortKBufferOdd, 1, cmd);
					device->BindUAV(&gs_buffers.sortVBufferEven, 2, cmd);
					device->BindUAV(&gs_buffers.sortVBufferOdd, 3, cmd);
				}
				else {
					device->BindUAV(&gs_buffers.sortKBufferOdd, 0, cmd);
					device->BindUAV(&gs_buffers.sortKBufferEven, 1, cmd);
					device->BindUAV(&gs_buffers.sortVBufferOdd, 2, cmd);
					device->BindUAV(&gs_buffers.sortVBufferEven, 3, cmd);
				}
				device->BindUAV(&gs_buffers.sortHistBuffer, 4, cmd);

				device->PushConstants(&gaussian_radix, sizeof(GaussianRadixConstants), cmd);
				device->Dispatch(invocationSize, 1, 1, cmd);
				BarrierStackFlush(cmd);
			}

			device->BindUAV(&unbind, 0, cmd);
			device->BindUAV(&unbind, 1, cmd);
			device->BindUAV(&unbind, 2, cmd);
			device->BindUAV(&unbind, 3, cmd);

			// tile boundary test
			// 
			//graphics::GPUBuffer tileBoundaryBuffer;
			//GPUBufferDesc bd;
			//bd.size = tileX * tileY * sizeof(uint) * 2;
			//bd.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			//bd.misc_flags = ResourceMiscFlag::BUFFER_RAW;
			//bool success = device->CreateBuffer(&bd, nullptr, &tileBoundaryBuffer);
			//assert(success);
			//device->SetName(&tileBoundaryBuffer, "tileBoundaryBuffer");

			device->BindResource(&gs_buffers.sortKBufferEven, 0, cmd);	// t0
			device->BindUAV(&gs_buffers.tileBoundaryBuffer, 0, cmd);	// u0

			gaussian_push.num_gaussians = numInstances;

			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.tileBoundaryBuffer, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));
			BarrierStackFlush(cmd);

			device->BindComputeShader(&shaders[CSTYPE_GS_IDENTIFY_TILE_RANGES], cmd);
			device->PushConstants(&gaussian_push, sizeof(GaussianPushConstants), cmd);
			// in tileboundary hlsl, you can use numGaussains
			UINT numInstancesGroups = (numInstances + 255) / 256;

			device->Dispatch(
				numInstancesGroups,
				1,
				1,
				cmd
			);

			//=========================================================
			//test210 - render 
			if (rtMain.IsValid())
			{
				device->BindUAV(&rtMain, 0, cmd); // u0 
				device->BindUAV(&gs_buffers.totalSumBufferHost, 1, cmd); // u1

				device->BindResource(&gs_buffers.gaussianKernelAttributes, 0, cmd); // t0
				device->BindResource(srcBuffer, 1, cmd);
				device->BindResource(&gs_buffers.sortVBufferEven, 2, cmd); // t2
				device->BindResource(&gs_buffers.touchedTiles_0, 3, cmd); // t3
			}
			else
			{
				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);

				device->BindResource(&unbind, 0, cmd);
				device->BindResource(&unbind, 1, cmd);
				device->BindResource(&unbind, 2, cmd);
				device->BindResource(&unbind, 3, cmd);
			}

			barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));
			BarrierStackFlush(cmd);

			device->BindComputeShader(&shaders[CSTYPE_GS_RENDER_GAUSSIAN], cmd);
			device->PushConstants(&gaussian_sort, sizeof(GaussianSortConstants), cmd);

			device->Dispatch(
				numGroups,
				1,
				1,
				cmd
			);

			device->BindUAV(&unbind, 0, cmd);
			device->BindUAV(&unbind, 1, cmd);

			device->BindResource(&unbind, 0, cmd);
			device->BindResource(&unbind, 1, cmd);
			device->BindResource(&unbind, 2, cmd);
			device->BindResource(&unbind, 3, cmd);


			barrierStack.push_back(GPUBarrier::Image(&rtMain, ResourceState::UNORDERED_ACCESS, rtMain.desc.layout));

			BarrierStackFlush(cmd);
			//=========================================================


			////=========================================================
			//// render CS 
			//gaussian_sort.tileX = rtMain.desc.width;
			//gaussian_sort.tileY = rtMain.desc.height;


			//if (rtMain.IsValid())
			//{
			//	device->BindUAV(&rtMain, 0, cmd); // u0 
			//	device->BindResource(&gs_buffers.gaussianKernelAttributes, 0, cmd); // t0
			//	device->BindResource(&tileBoundaryBuffer, 1, cmd);		// t1
			//	device->BindResource(&gs_buffers.sortVBufferEven, 2, cmd);			// t2
			//}
			//else
			//{
			//	device->BindUAV(&unbind, 0, cmd);
			//	device->BindResource(&unbind, 0, cmd);
			//	device->BindResource(&unbind, 1, cmd);
			//	device->BindResource(&unbind, 2, cmd);
			//}

			//barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));
			//BarrierStackFlush(cmd);

			//device->BindComputeShader(&shaders[CSTYPE_GS_RENDER_GAUSSIAN], cmd);
			//device->PushConstants(&gaussian_sort, sizeof(GaussianSortConstants), cmd);
			//device->Dispatch(
			//	(width + 15) / GSPLAT_TILESIZE,
			//	(height + 15) / GSPLAT_TILESIZE,
			//	1, 
			//	cmd
			//);

			//device->BindUAV(&unbind, 0, cmd);
			//device->BindResource(&unbind, 0, cmd);
			//device->BindResource(&unbind, 1, cmd);
			//device->BindResource(&unbind, 2, cmd);

			//barrierStack.push_back(GPUBarrier::Image(&rtMain, ResourceState::UNORDERED_ACCESS, rtMain.desc.layout));

			//BarrierStackFlush(cmd);
			//// ========================================================

			/**/
		}

		device->EventEnd(cmd);
		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
}