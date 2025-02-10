#include "RenderPath3D_Details.h"

namespace vz::renderer
{
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
		auto range = profiler::BeginRangeGPU("RenderDirectVolumes", &cmd);

		BindCommonResources(cmd);

		uint32_t filterMask = GMaterialComponent::FILTER_VOLUME;

		// Note: the tile_count here must be valid whether the ViewResources was created or not!
		XMUINT2 tile_count = GetViewTileCount(XMUINT2(rtMain.desc.width, rtMain.desc.height));

		GPUResource unbind;

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
			renderQueue.sort_opaque();
			//renderQueue.sort_transparent();
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

				const XMUINT3& block_pitch = volume->GetBlockPitch();
				XMFLOAT3 vol_size = XMFLOAT3((float)volume->GetWidth(), (float)volume->GetHeight(), (float)volume->GetDepth());
				volume_push.singleblock_size_ts = XMFLOAT3((float)block_pitch.x / vol_size.x,
					(float)block_pitch.y / vol_size.y, (float)block_pitch.z / vol_size.z);

				volume_push.mask_value_range = 255.f;
				const XMFLOAT2& min_max_stored_v = volume->GetStoredMinMax();
				volume_push.value_range = min_max_stored_v.y - min_max_stored_v.x;
				volume_push.mask_unormid_otf_map = volume_push.mask_value_range / (otf->GetHeight() > 1 ? otf->GetHeight() - 1 : 1.f);

				const XMUINT3& blocks_size = volume->GetBlocksSize();
				volume_push.blocks_w = blocks_size.x;
				volume_push.blocks_wh = blocks_size.x * blocks_size.y;
			}

			if (rtMain.IsValid() && rtLinearDepth.IsValid())
			{
				device->BindUAV(&rtMain, 0, cmd);
				device->BindUAV(&rtLinearDepth, 1, cmd, 0);
			}
			else
			{
				device->BindUAV(&unbind, 0, cmd);
				device->BindUAV(&unbind, 1, cmd);
			}

			barrierStack.push_back(GPUBarrier::Image(&rtMain, rtMain.desc.layout, ResourceState::UNORDERED_ACCESS));
			barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS));
			BarrierStackFlush(cmd);

			device->BindComputeShader(&shaders[CSTYPE_DVR_DEFAULT], cmd);
			device->PushConstants(&volume_push, sizeof(VolumePushConstants), cmd);

			device->Dispatch(
				tile_count.x,
				tile_count.y,
				1,
				cmd
			);

			device->BindUAV(&unbind, 0, cmd);
			device->BindUAV(&unbind, 1, cmd);

			barrierStack.push_back(GPUBarrier::Image(&rtMain, ResourceState::UNORDERED_ACCESS, rtMain.desc.layout));
			barrierStack.push_back(GPUBarrier::Image(&rtLinearDepth, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE));
			BarrierStackFlush(cmd);

			break; // TODO: at this moment, just a single volume is supported!
		}


		device->EventEnd(cmd);
	}
}