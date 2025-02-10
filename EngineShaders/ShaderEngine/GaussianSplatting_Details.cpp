#include "RenderPath3D_Details.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::CreateTiledGaussianResources(GaussianSplattingResources& res, XMUINT2 resolution)
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

		device->EventBegin("Gaussian Splatting Render", cmd);
		auto range = profiler::BeginRangeGPU("RenderGaussianSplatting", &cmd);

		BindCommonResources(cmd);

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
			GGeometryComponent::GaussianSplattingBuffers& gs_buffers = geometry.GetGPrimBuffer(0)->gaussianSplattingBuffers;

			{
				//GGeometryComponent::GaussianSplattingBuffers& gs_buffers = geometry.GetGPrimBuffer(0)->gaussianSplattingBuffers;

				gaussian_push.instanceIndex = batch.instanceIndex;
				gaussian_push.geometryIndex = batch.geometryIndex;
				//gaussian_push.materialIndex = material->m
				gaussian_push.gaussian_SHs_index = device->GetDescriptorIndex(&gs_buffers.gaussianSHs, SubresourceType::SRV);
				gaussian_push.gaussian_scale_opacities_index = device->GetDescriptorIndex(&gs_buffers.gaussianScale_Opacities, SubresourceType::SRV);
				gaussian_push.gaussian_quaternions_index = device->GetDescriptorIndex(&gs_buffers.gaussianQuaterinions, SubresourceType::SRV);
				gaussian_push.touchedTiles_0_index = device->GetDescriptorIndex(&gs_buffers.touchedTiles_0, SubresourceType::UAV);
				gaussian_push.offsetTiles_0_index = device->GetDescriptorIndex(&gs_buffers.offsetTiles_0, SubresourceType::UAV);
				gaussian_push.duplicatedDepthGaussians_index = device->GetDescriptorIndex(&gs_buffers.duplicatedDepthGaussians, SubresourceType::UAV);
				gaussian_push.duplicatedTileDepthGaussians_0_index = device->GetDescriptorIndex(&gs_buffers.duplicatedTileDepthGaussians_0, SubresourceType::UAV);
				gaussian_push.duplicatedIdGaussians_index = device->GetDescriptorIndex(&gs_buffers.duplicatedIdGaussians, SubresourceType::UAV);

				gaussian_push.num_gaussians = geometry.GetPrimitive(0)->GetNumVertices();
			}

			//if (rtMain.IsValid() && rtLinearDepth.IsValid())
			//{
			//	device->BindUAV(&rtMain, 0, cmd);
			//	device->BindUAV(&rtLinearDepth, 1, cmd, 0);
			//}
			//else
			//{
			//	device->BindUAV(&unbind, 0, cmd);
			//	device->BindUAV(&unbind, 1, cmd);
			//}

			// t0, t1 SRV(rot, opacity, scale)
			//device->BindResource(&gs_buffers.gaussianQuaterinions, 0, cmd); // t0
			//device->BindResource(&gs_buffers.gaussianScale_Opacities, 1, cmd); //t1

			if (rtMain.IsValid())
			{
				device->BindUAV(&rtMain, 0, cmd);
				device->BindUAV(&gs_buffers.touchedTiles_0, 1, cmd); // touched tiles count 
				device->BindUAV(&gs_buffers.offsetTiles_0, 2, cmd); // prefix sum of touched tiles count
			}
			else
			{
				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
				device->BindUAV(&unbind, 2, cmd);
			}

			barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.touchedTiles_0, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.offsetTiles_0, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));

			BarrierStackFlush(cmd);

			uint P = gaussian_push.num_gaussians;

			int threads_per_group = 256;
			int numGroups = (P + threads_per_group - 1) / threads_per_group; // num_groups

			// preprocess and calculate touched tiles count
			device->BindComputeShader(&shaders[CSTYPE_GS_GAUSSIAN_TOUCH_COUNT], cmd);
			device->PushConstants(&gaussian_push, sizeof(GaussianPushConstants), cmd);
			device->Dispatch(
				numGroups,
				1,
				1,
				cmd
			);

			// prefix sum (offset)
			device->BindComputeShader(&shaders[CSTYPE_GS_GAUSSIAN_OFFSET], cmd);
			//device->PushConstants(&gaussian_push, sizeof(GaussianPushConstants), cmd);
			device->Dispatch(
				numGroups,
				1,
				1,
				cmd
			);
			//
			//device->BindComputeShader(&shaders[CSTYPE_GS_DUPLICATED_GAUSSIANS], cmd);
			//device->Dispatch(
			//	gs_tile_count.x,
			//	gs_tile_count.y,
			//	1,
			//	cmd
			//);
			//
			//device->BindComputeShader(&shaders[CSTYPE_GS_SORT_DUPLICATED_GAUSSIANS], cmd);
			//device->Dispatch(
			//	gs_tile_count.x,
			//	gs_tile_count.y,
			//	1,
			//	cmd
			//);

			device->BindUAV(&unbind, 0, cmd);
			device->BindUAV(&unbind, 1, cmd);
			device->BindUAV(&unbind, 2, cmd);

			// unbind SRV??
			//device->BindResource(&unbind, 0, cmd);
			//device->BindResource(&unbind, 1, cmd);

			barrierStack.push_back(GPUBarrier::Image(&rtMain, ResourceState::UNORDERED_ACCESS, rtMain.desc.layout));
			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.touchedTiles_0, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
			barrierStack.push_back(GPUBarrier::Buffer(&gs_buffers.offsetTiles_0, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));

			BarrierStackFlush(cmd);

			break; // TODO: at this moment, just a single gs is supported!
		}

		device->EventEnd(cmd);
	}
}