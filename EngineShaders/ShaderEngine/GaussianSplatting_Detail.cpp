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

		bd.bind_flags = BindFlag::UNORDERED_ACCESS;
		bd.misc_flags = ResourceMiscFlag::INDIRECT_ARGS | ResourceMiscFlag::BUFFER_RAW;
		bd.size = sizeof(IndirectDispatchArgs);
		device->CreateBuffer(&bd, nullptr, &res.indirectBuffer);
		device->SetName(&res.indirectBuffer, "GaussianSplattingResources::indirectBuffer");
	}

	void GRenderPath3DDetails::RenderGaussianSplatting(CommandList cmd)
	{
		if (camera->IsOrtho())
		{
			return;
		}

		if (visMain.visibleRenderables_GSplat.empty())
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
		for (uint32_t instanceIndex : visMain.visibleRenderables_GSplat)
		{
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[instanceIndex];
			assert(renderable.GetRenderableType() == RenderableType::GSPLAT_RENDERABLE);
			if (!renderable.layeredmask->IsVisibleWith(visMain.layeredmask->GetVisibleLayerMask()))
				continue;
			if ((renderable.materialFilterFlags & filterMask) == 0)
				continue;

			GGeometryComponent& geometry = *renderable.geometry;
			if (!geometry.allowGaussianSplatting)
				continue;

			const float distance = math::Distance(visMain.camera->GetWorldEye(), renderable.GetAABB().getCenter());
			if (distance > renderable.GetFadeDistance() + renderable.GetAABB().getRadius())
				continue;

			renderQueue.add(renderable.geometry->geometryIndex, instanceIndex, distance, renderable.sortBits);
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
			const uint32_t renderable_index = batch.GetRenderableIndex();	// renderable index (base renderable)
			const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[renderable_index];
			assert(renderable.GetRenderableType() == RenderableType::GSPLAT_RENDERABLE);

			GMaterialComponent* material = renderable.materials[0];
			assert(material);

			GGeometryComponent& geometry = *renderable.geometry;
			GPrimBuffers* gprim_buffer = geometry.GetGPrimBuffer(0);
			const Primitive* primitive = geometry.GetPrimitive(0);

			UINT width = rtMain.desc.width;
			UINT height = rtMain.desc.height;

			UINT tileWidth = (width + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE;
			UINT tileHeight = (height + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE;

			static const uint32_t GAUSSIAN_SH = gprim_buffer->customBufferMap["gaussianSHs"];
			static const uint32_t GAUSSIAN_SO = gprim_buffer->customBufferMap["gaussianScale_Opacities"];
			static const uint32_t GAUSSIAN_QT = gprim_buffer->customBufferMap["gaussianQuaterinions"];
			static const uint32_t GAUSSIAN_RENDER_ATTRIBUTE = gprim_buffer->customBufferMap["gaussianKernelAttributes"];
			static const uint32_t GAUSSIAN_OFFSET_TILES = gprim_buffer->customBufferMap["offsetTiles"];
			static const uint32_t GAUSSIAN_REPLICATE_KEY = gprim_buffer->customBufferMap["gaussianReplicationKey"];
			static const uint32_t GAUSSIAN_REPLICATE_VALUE = gprim_buffer->customBufferMap["gaussianReplicationValue"];
			static const uint32_t GAUSSIAN_SORTED_INDICES = gprim_buffer->customBufferMap["sortedIndices"];
			static const uint32_t GAUSSIAN_COUNTER = gprim_buffer->customBufferMap["gaussianCounterBuffer"];
			static const uint32_t GAUSSIAN_COUNTER_READBACK_0 = gprim_buffer->customBufferMap["gaussianCounterBuffer_readback_0"];
			static const uint32_t GAUSSIAN_COUNTER_READBACK_1 = gprim_buffer->customBufferMap["gaussianCounterBuffer_readback_1"];

			GPUBuffer& gaussianCounterBuffer = gprim_buffer->customBuffers[GAUSSIAN_COUNTER];
			GPUBuffer& gaussianKernelAttributes = gprim_buffer->customBuffers[GAUSSIAN_RENDER_ATTRIBUTE];
			GPUBuffer& offsetTiles = gprim_buffer->customBuffers[GAUSSIAN_OFFSET_TILES];
			GPUBuffer& gaussianScale_Opacities = gprim_buffer->customBuffers[GAUSSIAN_SO];
			GPUBuffer& gaussianQuaterinions = gprim_buffer->customBuffers[GAUSSIAN_QT];
			GPUBuffer& gaussianSHs = gprim_buffer->customBuffers[GAUSSIAN_SH];
			GPUBuffer& replicatedGaussianKey = gprim_buffer->customBuffers[GAUSSIAN_REPLICATE_KEY];
			GPUBuffer& replicatedGaussianValue = gprim_buffer->customBuffers[GAUSSIAN_REPLICATE_VALUE];
			GPUBuffer& sortedIndices = gprim_buffer->customBuffers[GAUSSIAN_SORTED_INDICES];
			GPUBuffer gaussianCounterBuffer_readback[2] = {
				gprim_buffer->customBuffers[GAUSSIAN_COUNTER_READBACK_0],
				gprim_buffer->customBuffers[GAUSSIAN_COUNTER_READBACK_1]
			};
			GPUBuffer& tileGaussianRange = gaussianSplattingResources.tileGaussianRange;

			GaussianPushConstants gsplat_push;
			
			// ------ kickoff -----
			{
				barrierStack.push_back(GPUBarrier::Buffer(&gaussianCounterBuffer, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
				barrierStack.push_back(GPUBarrier::Buffer(&tileGaussianRange, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
				BarrierStackFlush(cmd);
			}
			device->ClearUAV(&gaussianCounterBuffer, 0u, cmd);
			device->ClearUAV(&tileGaussianRange, ~0u, cmd);
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

					barrierStack.push_back(GPUBarrier::Buffer(&gaussianKernelAttributes, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					barrierStack.push_back(GPUBarrier::Buffer(&offsetTiles, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));

					barrierStack.push_back(GPUBarrier::Buffer(&gaussianScale_Opacities, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gaussianQuaterinions, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gaussianSHs, ResourceState::UNDEFINED, ResourceState::SHADER_RESOURCE_COMPUTE));
					BarrierStackFlush(cmd);
				}
				//device->BindUAV(&rtMain, 10, cmd); // just for debug

				//device->BindUAV(&touchedTiles, 0, cmd);
				device->BindUAV(&gaussianKernelAttributes, 1, cmd);
				device->BindUAV(&offsetTiles, 2, cmd);
				device->BindUAV(&gaussianCounterBuffer, 3, cmd);

				device->BindResource(&gaussianScale_Opacities, 0, cmd);
				device->BindResource(&gaussianQuaterinions, 1, cmd);
				device->BindResource(&gaussianSHs, 2, cmd);

				gsplat_push.instanceIndex = batch.instanceIndex;
				gsplat_push.tileWidth = tileWidth;
				gsplat_push.tileHeight = tileHeight;
				gsplat_push.numGaussians = num_gaussians;
				gsplat_push.geometryIndex = gprim_buffer->vbPosW.descriptor_srv;
				gsplat_push.flags = 0u;// GSPLAT_FLAG_ANTIALIASING;

				if (camera->IsIntrinsicsProjection())
				{
					camera->GetIntrinsics(&gsplat_push.focalX, &gsplat_push.focalY, nullptr, nullptr, nullptr);
				}
				else 
				{
					float fov_vertical = camera->GetFovVertical();
					float aspect_ratio = (float)width / (float)height;
					float fov_horizon = 2.f * atan(fov_vertical / 2.f) * aspect_ratio;
					gsplat_push.focalX = (float)width / (2.f * tan(fov_horizon / 2.f));
					gsplat_push.focalY = (float)height / (2.f * tan(fov_vertical / 2.f));
				}

				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_PREPROCESS], cmd);
				device->PushConstants(&gsplat_push, sizeof(GaussianPushConstants), cmd);
				device->Dispatch(
					num_groups,
					1,
					1,
					cmd
				);

				device->BindUAV(&unbind, 10, cmd);
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
				barrierStack.push_back(GPUBarrier::Buffer(&gaussianCounterBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SRC));
				BarrierStackFlush(cmd);
			}
			uint32_t write_index = device->GetBufferIndex();
			uint32_t read_index = (write_index + 1) % device->GetBufferCount();
			device->CopyResource(
				&gaussianCounterBuffer_readback[write_index],
				&gaussianCounterBuffer,
				cmd
			);
			const uint32_t* counter_gsplat = (const uint32_t*)gaussianCounterBuffer_readback[read_index].mapped_data;
			uint32_t num_gaussian_replications = counter_gsplat[0];
			if (num_gaussian_replications == 0)
				break;
			
			uint32_t num_gaussian_replications_exist = (uint32_t)(replicatedGaussianValue.desc.size / sizeof(uint32_t));
			if (num_gaussian_replications > num_gaussian_replications_exist)
			{
				uint32_t num_replicate_kernels = num_gaussian_replications * 2;
				vzlog("Capacity for Gaussian replicates update: request (%d) and allocate (%d).. prev (%d)", num_gaussian_replications, num_replicate_kernels, num_gaussian_replications_exist);

				GPUBufferDesc bd;
				if (device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
				{
					// In UMA mode, it is better to create UPLOAD buffer, this avoids one copy from UPLOAD to DEFAULT
					bd.usage = Usage::UPLOAD;
				}
				else
				{
					bd.usage = Usage::DEFAULT;
				}
				bd.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				bd.misc_flags = ResourceMiscFlag::BUFFER_RAW;

				bd.size = num_replicate_kernels * sizeof(UINT) * 2; // uint_64
				assert(device->CreateBuffer(&bd, nullptr, &replicatedGaussianKey));
				device->SetName(&replicatedGaussianKey, "GaussianSplattingBuffers::replicatedGaussianKey");

				bd.size = num_replicate_kernels * sizeof(UINT);
				assert(device->CreateBuffer(&bd, nullptr, &replicatedGaussianValue));
				device->SetName(&replicatedGaussianValue, "GaussianSplattingBuffers::replicatedGaussianValue");

				bd.size = num_replicate_kernels * sizeof(UINT);
				assert(device->CreateBuffer(&bd, nullptr, &sortedIndices));
				device->SetName(&sortedIndices, "GaussianSplattingBuffers::sortedIndices");
			}

//#define DEBUG_GSPLAT
#ifdef DEBUG_GSPLAT
			static uint32_t size_debug_buffer = 0;
			if (size_debug_buffer < num_gaussian_replications)
			{
				size_debug_buffer = capacityGaussians;

				GPUBufferDesc bd = replicatedGaussianKey.desc;
				bd.usage = Usage::READBACK;
				bd.bind_flags = BindFlag::NONE;
				bd.misc_flags = ResourceMiscFlag::NONE;
				for (int i = 0; i < arraysize(gaussianSplattingResources.debugBuffer_readback); ++i)
				{
					device->CreateBuffer(&bd, nullptr, &gaussianSplattingResources.debugBuffer_readback[i]);
					device->SetName(&gaussianSplattingResources.debugBuffer_readback[i], "debugBuffer_readback");
				}
			}
#endif
			// ------ indirect setting ------
			{
				{
					barrierStack.push_back(GPUBarrier::Buffer(&gaussianCounterBuffer, ResourceState::COPY_SRC, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gaussianSplattingResources.indirectBuffer, ResourceState::INDIRECT_ARGUMENT, ResourceState::UNORDERED_ACCESS));
					BarrierStackFlush(cmd);
				}

				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_INDIRECT_SETTING], cmd);

				device->BindUAV(&gaussianSplattingResources.indirectBuffer, 0, cmd);
				device->BindResource(&gaussianCounterBuffer, 0, cmd);
				device->Dispatch(1, 1, 1, cmd);

				{
					barrierStack.push_back(GPUBarrier::Buffer(&gaussianSplattingResources.indirectBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::INDIRECT_ARGUMENT));
					BarrierStackFlush(cmd);
				}
				device->BindUAV(&unbind, 0, cmd);
				device->BindResource(&unbind, 0, cmd);
			}

			// ------ replication -----
			device->EventBegin("GaussianSplatting - Replication", cmd);
			{
				{
					//barrierStack.push_back(GPUBarrier::Buffer(&gaussianCounterBuffer, ResourceState::COPY_SRC, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&offsetTiles, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Buffer(&gaussianKernelAttributes, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					
					barrierStack.push_back(GPUBarrier::Buffer(&sortedIndices, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					barrierStack.push_back(GPUBarrier::Buffer(&replicatedGaussianKey, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					barrierStack.push_back(GPUBarrier::Buffer(&replicatedGaussianValue, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					BarrierStackFlush(cmd);
				}

				device->BindUAV(&replicatedGaussianKey, 0, cmd);
				device->BindUAV(&replicatedGaussianValue, 1, cmd);
				device->BindUAV(&sortedIndices, 2, cmd);

				device->BindResource(&gaussianKernelAttributes, 0, cmd);
				device->BindResource(&offsetTiles, 1, cmd);
				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_REPLICATE_GAUSSIANS], cmd);

				device->PushConstants(&gsplat_push, sizeof(GaussianPushConstants), cmd);
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
					barrierStack.push_back(GPUBarrier::Buffer(&replicatedGaussianKey, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					barrierStack.push_back(GPUBarrier::Memory(&sortedIndices));
					BarrierStackFlush(cmd);
				}
				gpusortlib::Sort(num_gaussian_replications * 2, gpusortlib::COMPARISON_UINT64, replicatedGaussianKey, gaussianCounterBuffer, 0,
					sortedIndices, cmd);
			}
			device->EventEnd(cmd);


#ifdef DEBUG_GSPLAT
			// ----- readback replication count -----
			GPUBuffer& debug_src_buffer = replicatedGaussianValue;
			{
				barrierStack.push_back(GPUBarrier::Buffer(&debug_src_buffer, ResourceState::UNORDERED_ACCESS, ResourceState::COPY_SRC));
				BarrierStackFlush(cmd);
			}
			device->CopyBuffer(&gaussianSplattingResources.debugBuffer_readback[pingplong_readback_index], 0,
				&debug_src_buffer, 0, debug_src_buffer.desc.size, cmd);
			//device->CopyResource(
			//	&gaussianSplattingResources.debugBuffer_readback[pingplong_readback_index],
			//	&debug_src_buffer,
			//	cmd
			//);
			uint32_t* data_debug = (uint32_t*)gaussianSplattingResources.debugBuffer_readback[pingplong_readback_index].mapped_data;
			std::vector<uint32_t> data_debug_vtr(1000);
			memcpy(data_debug_vtr.data(), data_debug, sizeof(uint32_t) * 2 * 500);
			{
				barrierStack.push_back(GPUBarrier::Buffer(&debug_src_buffer, ResourceState::COPY_SRC, ResourceState::UNORDERED_ACCESS));
				BarrierStackFlush(cmd);
			}
#endif

			// ------ Update Tile Range -----
			device->EventBegin("GaussianSplatting - Update Tile Range", cmd);
			{
				{
					//barrierStack.push_back(GPUBarrier::Buffer(&gaussianSplattingResources.tileGaussianRange, ResourceState::UNDEFINED, ResourceState::UNORDERED_ACCESS));
					barrierStack.push_back(GPUBarrier::Memory(&gaussianSplattingResources.tileGaussianRange));		// UAV cleared
					barrierStack.push_back(GPUBarrier::Buffer(&sortedIndices, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					BarrierStackFlush(cmd);
				}

				device->BindUAV(&gaussianSplattingResources.tileGaussianRange, 0, cmd);

				device->BindResource(&replicatedGaussianKey, 0, cmd);
				device->BindResource(&sortedIndices, 1, cmd);
				device->BindResource(&gaussianCounterBuffer, 2, cmd);

				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_IDENTIFY_TILE_RANGES], cmd);
				//device->Dispatch(
				//	uint32_t(num_gaussian_replications * 2 / GSPLAT_GROUP_SIZE) + 1u,
				//	1,
				//	1,
				//	cmd
				//);
				device->DispatchIndirect(&gaussianSplattingResources.indirectBuffer, 0, cmd);

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
					barrierStack.push_back(GPUBarrier::Buffer(&replicatedGaussianValue, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE));
					BarrierStackFlush(cmd);
				}

				device->BindUAV(&rtMain, 0, cmd);

				device->BindResource(&gaussianKernelAttributes, 0, cmd);
				device->BindResource(&gaussianSplattingResources.tileGaussianRange, 1, cmd);
				device->BindResource(&replicatedGaussianValue, 2, cmd);
				device->BindResource(&sortedIndices, 3, cmd);
				//device->BindResource(&replicatedGaussianKey, 4, cmd);

				device->BindComputeShader(&shaders[CSTYPE_GAUSSIANSPLATTING_BLEND_GAUSSIAN], cmd);
				device->PushConstants(&gsplat_push, sizeof(GaussianPushConstants), cmd);
				device->Dispatch(
					tileWidth + 1,
					tileHeight + 1,
					1,
					cmd
				);

				{
					barrierStack.push_back(GPUBarrier::Image(&rtMain, ResourceState::UNORDERED_ACCESS, rtMain.desc.layout));
					BarrierStackFlush(cmd);
				}

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