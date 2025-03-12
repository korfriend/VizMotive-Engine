#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::CreateGaussianResources(GaussianSplattingResources& res, XMUINT2 resolution)
	{
		res.tileCount = GetGaussianSplattingTileCount(resolution);

		// new version //
		GPUBufferDesc bd;
		bd.stride = sizeof(uint);
		bd.size = res.tileCount.x * res.tileCount.y * bd.stride * SHADER_ENTITY_TILE_BUCKET_COUNT * 2; // *2: opaque and transparent arrays
		bd.usage = Usage::DEFAULT;
		bd.bind_flags = BindFlag::UNORDERED_ACCESS | BindFlag::SHADER_RESOURCE;
		bd.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
		device->CreateBuffer(&bd, nullptr, &res.touchedTiles_tiledCounts);
		device->SetName(&res.touchedTiles_tiledCounts, "touchedTiles_tiledCounts");

		device->CreateBuffer(&bd, nullptr, &res.offsetTiles);
		device->SetName(&res.offsetTiles, "offsetTiles");

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
		XMUINT2 gs_tile_count = XMUINT2(
			(rtMain.desc.width + GS_TILESIZE - 1) / GS_TILESIZE,
			(rtMain.desc.height + GS_TILESIZE - 1) / GS_TILESIZE
		);

		GPUResource unbind;

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
		for (const RenderBatch& batch : renderQueue.batches) // Do not break out of this loop!
		{
			const uint32_t geometry_index = batch.GetGeometryIndex();	// geometry index
			const uint32_t renderable_index = batch.GetRenderableIndex();	// renderable index (base renderable)
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderable_index];
			assert(renderable.IsMeshRenderable());

			GMaterialComponent* material = (GMaterialComponent*)compfactory::GetMaterialComponent(renderable.GetMaterial(0));
			assert(material);

			GGeometryComponent& geometry = *scene_Gdetails->geometryComponents[geometry_index];

			GaussianPushConstants gaussian_push;
			GaussianSortConstants gaussian_sort; // timestamp and gaussian_Vertex_Attributes_index; test210
			GaussianRadixConstants gaussian_radix;

			UINT width = rtMain.desc.width;
			UINT height = rtMain.desc.height;

			UINT tileX = (width + 16 - 1) / 16;
			UINT tileY = (height + 16 - 1) / 16;

			gaussian_sort.tileX = tileX;
			gaussian_sort.num_gaussians = geometry.GetPrimitive(0)->GetNumVertices();
			gaussian_push.num_gaussians = geometry.GetPrimitive(0)->GetNumVertices();

			GGeometryComponent::GaussianSplattingBuffers& gs_buffers = geometry.GetGPrimBuffer(0)->gaussianSplattingBuffers;

			// test vertex attrs
			{
				gaussian_sort.gaussian_vertex_attributes_index = device->GetDescriptorIndex(&gs_buffers.gaussianKernelAttributes, SubresourceType::UAV);
			}

			{
				//GGeometryComponent::GaussianSplattingBuffers& gs_buffers = geometry.GetGPrimBuffer(0)->gaussianSplattingBuffers;
				gaussian_push.num_gaussians = geometry.GetPrimitive(0)->GetNumVertices();

				gaussian_push.instanceIndex = batch.instanceIndex;
				gaussian_push.geometryIndex = batch.geometryIndex;
				gaussian_push.gaussian_SHs_index = device->GetDescriptorIndex(&gs_buffers.gaussianSHs, SubresourceType::SRV);
				gaussian_push.gaussian_scale_opacities_index = device->GetDescriptorIndex(&gs_buffers.gaussianScale_Opacities, SubresourceType::SRV);
				gaussian_push.gaussian_quaternions_index = device->GetDescriptorIndex(&gs_buffers.gaussianQuaterinions, SubresourceType::SRV);
				gaussian_push.touchedTiles_0_index = device->GetDescriptorIndex(&gs_buffers.touchedTiles_0, SubresourceType::UAV);
				gaussian_push.offsetTiles_0_index = device->GetDescriptorIndex(&gs_buffers.offsetTiles_0, SubresourceType::UAV);

				// Ping and Pong Buffer for prefix sum
				gaussian_push.offsetTiles_Ping_index = device->GetDescriptorIndex(&gs_buffers.offsetTilesPing, SubresourceType::UAV);
				gaussian_push.offsetTiles_Pong_index = device->GetDescriptorIndex(&gs_buffers.offsetTilesPong, SubresourceType::UAV);

				// total sum buffer = prefixsum[P - 1]
				gaussian_sort.totalSumBufferHost_index = device->GetDescriptorIndex(&gs_buffers.totalSumBufferHost, SubresourceType::UAV);

				// duplicated with keys buffer
				gaussian_sort.sortKBufferEven_index = device->GetDescriptorIndex(&gs_buffers.sortKBufferEven, SubresourceType::UAV);
				gaussian_sort.sortKBufferOdd_index = device->GetDescriptorIndex(&gs_buffers.sortKBufferOdd, SubresourceType::UAV);
				gaussian_sort.sortVBufferEven_index = device->GetDescriptorIndex(&gs_buffers.sortVBufferEven, SubresourceType::UAV);
				gaussian_sort.sortVBufferOdd_index = device->GetDescriptorIndex(&gs_buffers.sortVBufferOdd, SubresourceType::UAV);

				gaussian_sort.sortHistBuffer_index = device->GetDescriptorIndex(&gs_buffers.sortHistBuffer, SubresourceType::UAV);
				gaussian_sort.tileBoundaryBuffer_index = device->GetDescriptorIndex(&gs_buffers.tileBoundaryBuffer, SubresourceType::UAV);

				// readback Buffer
				gaussian_push.readBackBufferTest_index = device->GetDescriptorIndex(&gs_buffers.readBackBufferTest, SubresourceType::UAV);
			}

			// preprocess
			if (rtMain.IsValid())
			{
				device->BindUAV(&rtMain, 0, cmd);
				device->BindUAV(&gs_buffers.touchedTiles_0, 1, cmd);			// touched tiles count 
				device->BindUAV(&gs_buffers.gaussianKernelAttributes, 2, cmd);  // vertex attributes
			}
			else
			{
				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
				device->BindUAV(&unbind, 2, cmd);
			}

			// SRV to UAV
			barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.touchedTiles_0, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.gaussianKernelAttributes, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));

			BarrierStackFlush(cmd);

			uint numGaussians = gaussian_push.num_gaussians;

			int threads_per_group = 256;
			int numGroups = (numGaussians + threads_per_group - 1) / threads_per_group; // num_groups

			// preprocess and calculate touched tiles count
			device->BindComputeShader(&shaders[CSTYPE_GS_PREPROCESS], cmd);
			device->PushConstants(&gaussian_push, sizeof(GaussianPushConstants), cmd);
			device->Dispatch(
				numGroups,
				1,
				1,
				cmd
			);

			device->BindUAV(&unbind, 0, cmd);
			device->BindUAV(&unbind, 1, cmd);
			device->BindUAV(&unbind, 2, cmd);
			// end preprocess here
		
			// copy touched tiles count to offset tiles
			{
				GPUBarrier barriers[] =
				{
					GPUBarrier::Buffer(&gs_buffers.touchedTiles_0, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SRC),
					GPUBarrier::Buffer(&gs_buffers.offsetTilesPing, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_DST)
				};
				device->Barrier(barriers, _countof(barriers), cmd);
			}

			device->CopyBuffer(
				&gs_buffers.offsetTilesPing,  // dst buffer
				0,
				&gs_buffers.touchedTiles_0,   // src buffer
				0,
				(numGaussians * sizeof(UINT)),
				cmd
			);

			{
				GPUBarrier barriers2[] =
				{
					GPUBarrier::Buffer(&gs_buffers.touchedTiles_0, ResourceState::COPY_SRC, ResourceState::UNORDERED_ACCESS),
					GPUBarrier::Buffer(&gs_buffers.offsetTilesPing, ResourceState::COPY_DST, ResourceState::UNORDERED_ACCESS)
				};
				device->Barrier(barriers2, _countof(barriers2), cmd);
			}

			// UAV to SRV
			barrierStack.push_back(GPUBarrier::Image(&rtMain, ResourceState::UNORDERED_ACCESS, rtMain.desc.layout));
			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.touchedTiles_0, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.gaussianKernelAttributes, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));

			// prefix sum (offset)
			UINT iters = (uint)std::ceil(std::log2((float)numGaussians));

			device->BindComputeShader(&shaders[CSTYPE_GS_GAUSSIAN_OFFSET], cmd);

			for (int step = 0; step <= iters; ++step) {
				gaussian_sort.timestamp = step;
				device->PushConstants(&gaussian_sort, sizeof(GaussianSortConstants), cmd);

				if ((step % 2) == 0)
				{
					// read : offsetTilesPing, write : offsetTilesPong
					barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.offsetTilesPing, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
					barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.offsetTilesPong, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));

					// unbind
					device->BindResource(&unbind, 0, cmd);	// t0
					device->BindResource(&unbind, 1, cmd);  // t1
					device->BindUAV(&unbind, 3, cmd);       // u3
					device->BindUAV(&unbind, 4, cmd);       // u4

					// SRV: offsetTilesPing ¡æ register(t0)
					device->BindResource(&gs_buffers.offsetTilesPing, 0, cmd);  // bind to t0
					// UAV: offsetTilesPong ¡æ register(u4)
					device->BindUAV(&gs_buffers.offsetTilesPong, 4, cmd);		// bind to u4
				}
				else
				{
					// read : offsetTilesPong, write : offsetTilesPing
					barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.offsetTilesPong, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
					barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.offsetTilesPing, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));

					device->BindResource(&unbind, 0, cmd);	// t0
					device->BindResource(&unbind, 1, cmd);  // t1
					device->BindUAV(&unbind, 3, cmd);       // u3
					device->BindUAV(&unbind, 4, cmd);       // u4

					// SRV: offsetTilesPong ¡æ register(t1)
					device->BindResource(&gs_buffers.offsetTilesPong, 1, cmd);  // bind to t1
					// UAV: offsetTilesPing ¡æ register(u3)
					device->BindUAV(&gs_buffers.offsetTilesPing, 3, cmd);		// bind to u3
				}
				device->Dispatch(numGroups, 1, 1, cmd);
				BarrierStackFlush(cmd);
			}

			device->BindResource(&unbind, 0, cmd);	// t0
			device->BindResource(&unbind, 1, cmd);  // t1
			device->BindUAV(&unbind, 3, cmd);       // u3
			device->BindUAV(&unbind, 4, cmd);       // u4

			if ((iters % 2) == 0)
				barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.offsetTilesPong, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
			else
				barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.offsetTilesPing, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));

			BarrierStackFlush(cmd);

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
			//	(width + 15) / 16,
			//	(height + 15) / 16,
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

			break; // TODO: at this moment, just a single gs is supported!
		}

		device->EventEnd(cmd);
		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
}