#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::ComputeVolumetricCloudShadows(
		CommandList cmd,
		const Texture* envMapFirst,
		const Texture* envMapSecond)
	{
		device->EventBegin("RenderVolumetricCloudShadows", cmd);
		auto range = profiler::BeginRangeGPU("Volumetric Clouds Shadow", &cmd);

		BindCommonResources(cmd);

		const TextureDesc& desc = textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW].GetDesc();
		PostProcess postprocess;
		postprocess.resolution.x = desc.width;
		postprocess.resolution.y = desc.height;
		postprocess.resolution_rcp.x = 1.0f / postprocess.resolution.x;
		postprocess.resolution_rcp.y = 1.0f / postprocess.resolution.y;

		// Cloud shadow render pass:
		{
			device->EventBegin("Volumetric Cloud Rendering Shadow", cmd);
			device->BindComputeShader(&shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_SHADOW_RENDER], cmd);
			device->PushConstants(&postprocess, sizeof(postprocess), cmd);

			device->BindResource(&textureShapeNoise, 0, cmd);
			device->BindResource(&textureDetailNoise, 1, cmd);
			device->BindResource(&textureCurlNoise, 2, cmd);

			if (envMapFirst != nullptr)
			{
				device->BindResource(envMapFirst, 3, cmd);
			}
			else
			{
				device->BindResource(&textureEnvMap, 3, cmd);
			}

			if (envMapSecond != nullptr)
			{
				device->BindResource(envMapSecond, 4, cmd);
			}
			else
			{
				device->BindResource(&textureEnvMap, 4, cmd);
			}

			const GPUResource* uavs[] = {
				&textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW],
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW], textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW].desc.layout, ResourceState::UNORDERED_ACCESS),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->Dispatch(
				(textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW].GetDesc().width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
				(textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW].GetDesc().height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
				1,
				cmd
			);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW], ResourceState::UNORDERED_ACCESS, textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW].desc.layout),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);
		}

		Postprocess_Blur_Gaussian(
			textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW],
			textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW_GAUSSIAN_TEMP],
			textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW],
			cmd,
			-1, -1,
			false // wide
		);

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
	void GRenderPath3DDetails::ComputeSkyAtmosphereTextures(CommandList cmd)
	{
		device->EventBegin("ComputeSkyAtmosphereTextures", cmd);
		auto range = profiler::BeginRangeGPU("SkyAtmosphere Textures", &cmd);

		BindCommonResources(cmd);

		// Transmittance Lut pass:
		{
			device->EventBegin("TransmittanceLut", cmd);
			device->BindComputeShader(&shaders[CSTYPE_SKYATMOSPHERE_TRANSMITTANCELUT], cmd);

			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT], 0, cmd); // empty
			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], 1, cmd); // empty
			device->BindResource(&textures[TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW], 2, cmd);

			const GPUResource* uavs[] = {
				&textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT],
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT], textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT].desc.layout, ResourceState::UNORDERED_ACCESS)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			const int threadSize = 8;
			const int transmittanceLutWidth = textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT].GetDesc().width;
			const int transmittanceLutHeight = textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT].GetDesc().height;
			const int transmittanceLutThreadX = static_cast<uint32_t>(std::ceil(transmittanceLutWidth / threadSize));
			const int transmittanceLutThreadY = static_cast<uint32_t>(std::ceil(transmittanceLutHeight / threadSize));

			device->Dispatch(transmittanceLutThreadX, transmittanceLutThreadY, 1, cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Memory(),
					GPUBarrier::Image(&textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT], ResourceState::UNORDERED_ACCESS, textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT].desc.layout)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);
		}

		// MultiScattered Luminance Lut pass:
		{
			device->EventBegin("MultiScatteredLuminanceLut", cmd);
			device->BindComputeShader(&shaders[CSTYPE_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], cmd);

			// Use transmittance from previous pass
			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT], 0, cmd);
			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], 1, cmd); // empty

			const GPUResource* uavs[] = {
				&textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT],
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT].desc.layout, ResourceState::UNORDERED_ACCESS)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			const int multiScatteredLutWidth = textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT].GetDesc().width;
			const int multiScatteredLutHeight = textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT].GetDesc().height;

			device->Dispatch(multiScatteredLutWidth, multiScatteredLutHeight, 1, cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Memory(),
					GPUBarrier::Image(&textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], ResourceState::UNORDERED_ACCESS, textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT].desc.layout)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);
		}

		// Environment Luminance Lut pass:
		{
			device->EventBegin("EnvironmentLuminanceLut", cmd);
			device->BindComputeShader(&shaders[CSTYPE_SKYATMOSPHERE_SKYLUMINANCELUT], cmd);

			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT], 0, cmd);
			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], 1, cmd);

			const GPUResource* uavs[] = {
				&textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT],
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT], textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT].desc.layout, ResourceState::UNORDERED_ACCESS)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			const int environmentLutWidth = textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT].GetDesc().width;
			const int environmentLutHeight = textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT].GetDesc().height;

			device->Dispatch(environmentLutWidth, environmentLutHeight, 1, cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Memory(),
					GPUBarrier::Image(&textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT], ResourceState::UNORDERED_ACCESS, textures[TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT].desc.layout)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);
		}

		device->EventEnd(cmd);

		profiler::EndRange(range);
	}
	void GRenderPath3DDetails::ComputeSkyAtmosphereSkyViewLut(CommandList cmd)
	{
		const int threadSize = 8;
		const int skyViewLutWidth = textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT].GetDesc().width;
		const int skyViewLutHeight = textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT].GetDesc().height;
		const int skyViewLutThreadX = static_cast<uint32_t>(std::ceil(skyViewLutWidth / threadSize));
		const int skyViewLutThreadY = static_cast<uint32_t>(std::ceil(skyViewLutHeight / threadSize));
		if (skyViewLutThreadX * skyViewLutThreadY < 1)
			return;

		device->EventBegin("ComputeSkyAtmosphereSkyViewLut", cmd);

		BindCommonResources(cmd);

		// Sky View Lut pass:
		{
			device->EventBegin("SkyViewLut", cmd);
			device->BindComputeShader(&shaders[CSTYPE_SKYATMOSPHERE_SKYVIEWLUT], cmd);

			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT], 0, cmd);
			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], 1, cmd);

			const GPUResource* uavs[] = {
				&textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT],
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT], textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT].desc.layout, ResourceState::UNORDERED_ACCESS)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->Dispatch(skyViewLutThreadX, skyViewLutThreadY, 1, cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT], ResourceState::UNORDERED_ACCESS, textures[TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT].desc.layout)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);
		}

		device->EventEnd(cmd);
	}
	void GRenderPath3DDetails::ComputeSkyAtmosphereCameraVolumeLut(CommandList cmd)
	{
		device->EventBegin("ComputeSkyAtmosphereCameraVolumeLut", cmd);

		BindCommonResources(cmd);

		// Camera Volume Lut pass:
		{
			device->EventBegin("CameraVolumeLut", cmd);
			device->BindComputeShader(&shaders[CSTYPE_SKYATMOSPHERE_CAMERAVOLUMELUT], cmd);

			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT], 0, cmd);
			device->BindResource(&textures[TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], 1, cmd);

			const GPUResource* uavs[] = {
				&textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT],
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT], textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT].desc.layout, ResourceState::UNORDERED_ACCESS)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			const int threadSize = 8;
			const int cameraVolumeLutWidth = textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT].GetDesc().width;
			const int cameraVolumeLutHeight = textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT].GetDesc().height;
			const int cameraVolumeLutDepth = textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT].GetDesc().depth;
			const int cameraVolumeLutThreadX = static_cast<uint32_t>(std::ceil(cameraVolumeLutWidth / threadSize));
			const int cameraVolumeLutThreadY = static_cast<uint32_t>(std::ceil(cameraVolumeLutHeight / threadSize));
			const int cameraVolumeLutThreadZ = static_cast<uint32_t>(std::ceil(cameraVolumeLutDepth / threadSize));

			device->Dispatch(cameraVolumeLutThreadX, cameraVolumeLutThreadY, cameraVolumeLutThreadZ, cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Memory(),
					GPUBarrier::Image(&textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT], ResourceState::UNORDERED_ACCESS, textures[TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT].desc.layout)
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);
		}

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::RefreshLightmaps(const Scene& scene, CommandList cmd)
	{
		/*
		if (!scene.IsLightmapUpdateRequested())
			return;
		if (!scene.TLAS.IsValid() && !scene.BVH.IsValid())
			return;

		jobsystem::Wait(raytracing_ctx);

		const uint32_t lightmap_request_count = scene.lightmap_request_allocator.load();

		auto range = profiler::BeginRangeGPU("Lightmap Processing", cmd);

		BindCommonResources(cmd);

		// Render lightmaps for each object:
		for (uint32_t requestIndex = 0; requestIndex < lightmap_request_count; ++requestIndex)
		{
			uint32_t objectIndex = *(scene.lightmap_requests.data() + requestIndex);
			const ObjectComponent& object = scene.objects[objectIndex];
			if (!object.lightmap.IsValid())
				continue;
			if (!object.lightmap_render.IsValid())
				continue;

			if (object.IsLightmapRenderRequested())
			{
				device->EventBegin("RenderObjectLightMap", cmd);

				const MeshComponent& mesh = scene.meshes[object.mesh_index];
				assert(!mesh.vertex_atlas.empty());
				assert(mesh.vb_atl.IsValid());

				const TextureDesc& desc = object.lightmap_render.GetDesc();

				static Texture lightmap_color_tmp;
				static Texture lightmap_depth_tmp;
				if (lightmap_color_tmp.desc.width < object.lightmap.desc.width || lightmap_color_tmp.desc.height < object.lightmap.desc.height)
				{
					lightmap_color_tmp.desc = object.lightmap.desc;
					lightmap_color_tmp.desc.misc_flags = ResourceMiscFlag::ALIASING_TEXTURE_RT_DS;
					device->CreateTexture(&lightmap_color_tmp.desc, nullptr, &lightmap_color_tmp);

					lightmap_depth_tmp.desc.width = object.lightmap.desc.width;
					lightmap_depth_tmp.desc.height = object.lightmap.desc.height;
					lightmap_depth_tmp.desc.format = Format::D16_UNORM;
					lightmap_depth_tmp.desc.bind_flags = BindFlag::DEPTH_STENCIL;
					lightmap_depth_tmp.desc.layout = ResourceState::DEPTHSTENCIL;
					device->CreateTexture(&lightmap_depth_tmp.desc, nullptr, &lightmap_depth_tmp, &lightmap_color_tmp); // aliased!
				}

				device->Barrier(GPUBarrier::Aliasing(&lightmap_color_tmp, &lightmap_depth_tmp), cmd);

				// Note: depth is used to disallow overlapped pixel/primitive writes with conservative rasterization!
				if (object.lightmapIterationCount == 0)
				{
					RenderPassImage rp[] = {
						RenderPassImage::RenderTarget(&object.lightmap_render, RenderPassImage::LoadOp::CLEAR),
						RenderPassImage::DepthStencil(&lightmap_depth_tmp, RenderPassImage::LoadOp::CLEAR),
					};
					device->RenderPassBegin(rp, arraysize(rp), cmd);
				}
				else
				{
					RenderPassImage rp[] = {
						RenderPassImage::RenderTarget(&object.lightmap_render, RenderPassImage::LoadOp::LOAD),
						RenderPassImage::DepthStencil(&lightmap_depth_tmp, RenderPassImage::LoadOp::CLEAR),
					};
					device->RenderPassBegin(rp, arraysize(rp), cmd);
				}

				Viewport vp;
				vp.width = (float)desc.width;
				vp.height = (float)desc.height;
				device->BindViewports(1, &vp, cmd);

				device->BindPipelineState(&PSO_renderlightmap, cmd);

				device->BindIndexBuffer(&mesh.generalBuffer, mesh.GetIndexFormat(), mesh.ib.offset, cmd);

				LightmapPushConstants push;
				push.vb_pos_wind = mesh.vb_pos_wind.descriptor_srv;
				push.vb_nor = mesh.vb_nor.descriptor_srv;
				push.vb_atl = mesh.vb_atl.descriptor_srv;
				push.instanceIndex = objectIndex;
				device->PushConstants(&push, sizeof(push), cmd);

				RaytracingCB cb = {};
				cb.xTraceResolution.x = desc.width;
				cb.xTraceResolution.y = desc.height;
				cb.xTraceResolution_rcp.x = 1.0f / cb.xTraceResolution.x;
				cb.xTraceResolution_rcp.y = 1.0f / cb.xTraceResolution.y;
				cb.xTraceAccumulationFactor = 1.0f / (object.lightmapIterationCount + 1.0f); // accumulation factor (alpha)
				cb.xTraceUserData.x = raytraceBounceCount;
				XMFLOAT4 halton = math::GetHaltonSequence(object.lightmapIterationCount); // for jittering the rasterization (good for eliminating atlas border artifacts)
				cb.xTracePixelOffset.x = (halton.x * 2 - 1) * cb.xTraceResolution_rcp.x;
				cb.xTracePixelOffset.y = (halton.y * 2 - 1) * cb.xTraceResolution_rcp.y;
				cb.xTracePixelOffset.x *= 1.4f;	// boost the jitter by a bit
				cb.xTracePixelOffset.y *= 1.4f;	// boost the jitter by a bit
				uint8_t instanceInclusionMask = 0xFF;
				cb.xTraceUserData.y = instanceInclusionMask;
				cb.xTraceSampleIndex = object.lightmapIterationCount;
				device->BindDynamicConstantBuffer(cb, CB_GETBINDSLOT(RaytracingCB), cmd);

				uint32_t indexStart = ~0u;
				uint32_t indexEnd = 0;

				uint32_t first_subset = 0;
				uint32_t last_subset = 0;
				mesh.GetLODSubsetRange(0, first_subset, last_subset);
				for (uint32_t subsetIndex = first_subset; subsetIndex < last_subset; ++subsetIndex)
				{
					const MeshComponent::MeshSubset& subset = mesh.subsets[subsetIndex];
					if (subset.indexCount == 0)
						continue;
					indexStart = std::min(indexStart, subset.indexOffset);
					indexEnd = std::max(indexEnd, subset.indexOffset + subset.indexCount);
				}

				if (indexEnd > indexStart)
				{
					const uint32_t indexCount = indexEnd - indexStart;
					device->DrawIndexed(indexCount, indexStart, 0, cmd);
					object.lightmapIterationCount++;
				}

				device->RenderPassEnd(cmd);

				device->Barrier(GPUBarrier::Aliasing(&lightmap_depth_tmp, &lightmap_color_tmp), cmd);

				// Expand opaque areas:
				{
					device->EventBegin("Lightmap expand", cmd);

					device->BindComputeShader(&shaders[CSTYPE_LIGHTMAP_EXPAND], cmd);

					// render -> lightmap
					{
						device->BindResource(&object.lightmap_render, 0, cmd);
						device->BindUAV(&object.lightmap, 0, cmd);

						device->Barrier(GPUBarrier::Image(&object.lightmap, object.lightmap.desc.layout, ResourceState::UNORDERED_ACCESS), cmd);

						device->Dispatch((desc.width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE, (desc.height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE, 1, cmd);

						device->Barrier(GPUBarrier::Image(&object.lightmap, ResourceState::UNORDERED_ACCESS, object.lightmap.desc.layout), cmd);
					}
					for (int repeat = 0; repeat < 2; ++repeat)
					{
						// lightmap -> temp
						{
							device->BindResource(&object.lightmap, 0, cmd);
							device->BindUAV(&lightmap_color_tmp, 0, cmd);

							device->Barrier(GPUBarrier::Image(&lightmap_color_tmp, lightmap_color_tmp.desc.layout, ResourceState::UNORDERED_ACCESS), cmd);

							device->Dispatch((desc.width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE, (desc.height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE, 1, cmd);

							device->Barrier(GPUBarrier::Image(&lightmap_color_tmp, ResourceState::UNORDERED_ACCESS, lightmap_color_tmp.desc.layout), cmd);
						}
						// temp -> lightmap
						{
							device->BindResource(&lightmap_color_tmp, 0, cmd);
							device->BindUAV(&object.lightmap, 0, cmd);

							device->Barrier(GPUBarrier::Image(&object.lightmap, object.lightmap.desc.layout, ResourceState::UNORDERED_ACCESS), cmd);

							device->Dispatch((desc.width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE, (desc.height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE, 1, cmd);

							device->Barrier(GPUBarrier::Image(&object.lightmap, ResourceState::UNORDERED_ACCESS, object.lightmap.desc.layout), cmd);
						}
					}
					device->EventEnd(cmd);
				}

				device->EventEnd(cmd);
			}
		}

		profiler::EndRange(range);
		/**/
	}
	void GRenderPath3DDetails::RefreshEnvProbes(const Visibility& vis, CommandList cmd)
	{
		device->EventBegin("EnvironmentProbe Refresh", cmd);
		auto range = profiler::BeginRangeGPU("Environment Probe Refresh", &cmd);

		BindCommonResources(cmd);

		float z_near_p, z_far_p;
		camera->GetNearFar(&z_near_p, &z_far_p);
		const float z_near_p_rcp = 1.0f / z_near_p;
		const float z_far_p_rcp = 1.0f / z_far_p;

		auto renderProbe = [&](const GProbeComponent& probe, const AABB& probeAABB) {

			GEnvironmentComponent* environment = scene_Gdetails->environment;

			Viewport vp;
			vp.height = vp.width = (float)probe.texture.desc.width;
			device->BindViewports(1, &vp, cmd);

			SHCAM cameras[6];
			CreateCubemapCameras(probe.position, z_near_p, z_far_p, cameras, arraysize(cameras));

			CameraCB cb;
			cb.Init();
			for (uint32_t i = 0; i < arraysize(cameras); ++i)
			{
				XMStoreFloat4x4(&cb.cameras[i].view_projection, cameras[i].view_projection);
				XMMATRIX invVP = XMMatrixInverse(nullptr, cameras[i].view_projection);
				XMStoreFloat4x4(&cb.cameras[i].inverse_view_projection, invVP);
				cb.cameras[i].position = probe.position;
				cb.cameras[i].output_index = i;
				cb.cameras[i].z_near = z_near_p;
				cb.cameras[i].z_near_rcp = z_near_p_rcp;
				cb.cameras[i].z_far = z_far_p;
				cb.cameras[i].z_far_rcp = z_far_p_rcp;
				cb.cameras[i].z_range = abs(z_far_p - z_near_p);
				cb.cameras[i].z_range_rcp = 1.0f / std::max(0.0001f, cb.cameras[i].z_range);
				cb.cameras[i].internal_resolution = uint2(probe.texture.desc.width, probe.texture.desc.height);
				cb.cameras[i].internal_resolution_rcp.x = 1.0f / cb.cameras[i].internal_resolution.x;
				cb.cameras[i].internal_resolution_rcp.y = 1.0f / cb.cameras[i].internal_resolution.y;
				cb.cameras[i].sample_count = probe.GetSampleCount();

				XMStoreFloat4(&cb.cameras[i].frustum_corners.cornersNEAR[0], XMVector3TransformCoord(XMVectorSet(-1, 1, 1, 1), invVP));
				XMStoreFloat4(&cb.cameras[i].frustum_corners.cornersNEAR[1], XMVector3TransformCoord(XMVectorSet(1, 1, 1, 1), invVP));
				XMStoreFloat4(&cb.cameras[i].frustum_corners.cornersNEAR[2], XMVector3TransformCoord(XMVectorSet(-1, -1, 1, 1), invVP));
				XMStoreFloat4(&cb.cameras[i].frustum_corners.cornersNEAR[3], XMVector3TransformCoord(XMVectorSet(1, -1, 1, 1), invVP));
				XMStoreFloat4(&cb.cameras[i].frustum_corners.cornersFAR[0], XMVector3TransformCoord(XMVectorSet(-1, 1, 0, 1), invVP));
				XMStoreFloat4(&cb.cameras[i].frustum_corners.cornersFAR[1], XMVector3TransformCoord(XMVectorSet(1, 1, 0, 1), invVP));
				XMStoreFloat4(&cb.cameras[i].frustum_corners.cornersFAR[2], XMVector3TransformCoord(XMVectorSet(-1, -1, 0, 1), invVP));
				XMStoreFloat4(&cb.cameras[i].frustum_corners.cornersFAR[3], XMVector3TransformCoord(XMVectorSet(1, -1, 0, 1), invVP));

			}
			device->BindDynamicConstantBuffer(cb, CBSLOT_RENDERER_CAMERA, cmd);

			if (scene_Gdetails->environment->IsRealisticSky())
			{
				// Update skyatmosphere textures, since each probe has different positions
				ComputeSkyAtmosphereSkyViewLut(cmd);
			}

			Texture envrenderingDepthBuffer;
			Texture envrenderingColorBuffer_MSAA;
			Texture envrenderingColorBuffer;
			Texture envrenderingColorBuffer_Filtered;

			// Find temporary render textures to fit request, or create new ones if they don't exist:
			union RenderTextureID
			{
				struct
				{
					uint32_t width : 16;
					uint32_t sample_count : 3;
					uint32_t is_depth : 1;
					uint32_t is_filtered : 1;
				} bits;
				uint32_t raw;
			};
			static std::unordered_map<uint32_t, Texture> render_textures;
			static std::mutex locker;
			{
				const uint32_t required_sample_count = probe.GetSampleCount();

				uint32_t resolution = probe.GetResolution();
				std::scoped_lock lck(locker);
				RenderTextureID id_depth = {};
				id_depth.bits.width = resolution;
				id_depth.bits.sample_count = required_sample_count;
				id_depth.bits.is_depth = 1;
				id_depth.bits.is_filtered = 0;
				envrenderingDepthBuffer = render_textures[id_depth.raw];

				RenderTextureID id_color = {};
				id_color.bits.width = resolution;
				id_color.bits.sample_count = 1;
				id_color.bits.is_depth = 0;
				id_color.bits.is_filtered = 0;
				envrenderingColorBuffer = render_textures[id_color.raw];

				RenderTextureID id_color_filtered = {};
				id_color_filtered.bits.width = resolution;
				id_color_filtered.bits.sample_count = 1;
				id_color_filtered.bits.is_depth = 0;
				id_color_filtered.bits.is_filtered = 1;
				envrenderingColorBuffer_Filtered = render_textures[id_color_filtered.raw];

				TextureDesc desc;
				desc.array_size = 6;
				desc.height = resolution;
				desc.width = resolution;
				desc.usage = Usage::DEFAULT;

				if (!envrenderingDepthBuffer.IsValid())
				{
					desc.mip_levels = 1;
					desc.bind_flags = BindFlag::DEPTH_STENCIL | BindFlag::SHADER_RESOURCE;
					desc.format = FORMAT_depthbufferEnvprobe;
					desc.layout = ResourceState::SHADER_RESOURCE;
					desc.sample_count = required_sample_count;
					if (required_sample_count == 1)
					{
						desc.misc_flags = ResourceMiscFlag::TEXTURECUBE;
					}
					device->CreateTexture(&desc, nullptr, &envrenderingDepthBuffer);
					device->SetName(&envrenderingDepthBuffer, "envrenderingDepthBuffer");
					render_textures[id_depth.raw] = envrenderingDepthBuffer;

					std::string info;
					info += "Created envprobe depth buffer for request";
					info += "\n\tResolution = " + std::to_string(desc.width) + " * " + std::to_string(desc.height) + " * 6";
					info += "\n\tSample Count = " + std::to_string(desc.sample_count);
					info += "\n\tMip Levels = " + std::to_string(desc.mip_levels);
					info += "\n\tFormat = ";
					info += GetFormatString(desc.format);
					info += "\n\tMemory = " + helper::GetMemorySizeText(ComputeTextureMemorySizeInBytes(desc)) + "\n";
					backlog::post(info);
				}

				if (!envrenderingColorBuffer.IsValid())
				{
					desc.mip_levels = probe.texture.desc.mip_levels;
					desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
					desc.format = FORMAT_rendertargetEnvprobe;
					desc.layout = ResourceState::SHADER_RESOURCE;
					desc.misc_flags = ResourceMiscFlag::TEXTURECUBE;
					desc.sample_count = 1;
					device->CreateTexture(&desc, nullptr, &envrenderingColorBuffer);
					device->SetName(&envrenderingColorBuffer, "envrenderingColorBuffer");
					render_textures[id_color.raw] = envrenderingColorBuffer;

					// Cubes per mip level:
					for (uint32_t i = 0; i < envrenderingColorBuffer.desc.mip_levels; ++i)
					{
						int subresource_index;
						subresource_index = device->CreateSubresource(&envrenderingColorBuffer, SubresourceType::SRV, 0, envrenderingColorBuffer.desc.array_size, i, 1);
						assert(subresource_index == i);
						subresource_index = device->CreateSubresource(&envrenderingColorBuffer, SubresourceType::UAV, 0, envrenderingColorBuffer.desc.array_size, i, 1);
						assert(subresource_index == i);
					}

					std::string info;
					info += "Created envprobe render target for request";
					info += "\n\tResolution = " + std::to_string(desc.width) + " * " + std::to_string(desc.height) + " * 6";
					info += "\n\tSample Count = " + std::to_string(desc.sample_count);
					info += "\n\tMip Levels = " + std::to_string(desc.mip_levels);
					info += "\n\tFormat = ";
					info += GetFormatString(desc.format);
					info += "\n\tMemory = " + helper::GetMemorySizeText(ComputeTextureMemorySizeInBytes(desc)) + "\n";
					backlog::post(info);
				}

				if (!envrenderingColorBuffer_Filtered.IsValid())
				{
					desc.mip_levels = probe.texture.desc.mip_levels;
					desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
					desc.format = FORMAT_rendertargetEnvprobe;
					desc.layout = ResourceState::SHADER_RESOURCE;
					desc.misc_flags = ResourceMiscFlag::TEXTURECUBE;
					desc.sample_count = 1;
					device->CreateTexture(&desc, nullptr, &envrenderingColorBuffer_Filtered);
					device->SetName(&envrenderingColorBuffer_Filtered, "envrenderingColorBuffer_Filtered");
					render_textures[id_color_filtered.raw] = envrenderingColorBuffer_Filtered;

					// Cubes per mip level:
					for (uint32_t i = 0; i < envrenderingColorBuffer_Filtered.desc.mip_levels; ++i)
					{
						int subresource_index;
						subresource_index = device->CreateSubresource(&envrenderingColorBuffer_Filtered, SubresourceType::SRV, 0, envrenderingColorBuffer_Filtered.desc.array_size, i, 1);
						assert(subresource_index == i);
						subresource_index = device->CreateSubresource(&envrenderingColorBuffer_Filtered, SubresourceType::UAV, 0, envrenderingColorBuffer_Filtered.desc.array_size, i, 1);
						assert(subresource_index == i);
					}

					std::string info;
					info += "Created envprobe filtering target for request";
					info += "\n\tResolution = " + std::to_string(desc.width) + " * " + std::to_string(desc.height) + " * 6";
					info += "\n\tSample Count = " + std::to_string(desc.sample_count);
					info += "\n\tMip Levels = " + std::to_string(desc.mip_levels);
					info += "\n\tFormat = ";
					info += GetFormatString(desc.format);
					info += "\n\tMemory = " + helper::GetMemorySizeText(ComputeTextureMemorySizeInBytes(desc)) + "\n";
					backlog::post(info);
				}

				if (required_sample_count > 1)
				{
					RenderTextureID id_color_msaa = {};
					id_color_msaa.bits.width = resolution;
					id_color_msaa.bits.sample_count = required_sample_count;
					id_color_msaa.bits.is_depth = 0;
					id_color_msaa.bits.is_filtered = 0;
					envrenderingColorBuffer_MSAA = render_textures[id_color_msaa.raw];

					if (!envrenderingColorBuffer_MSAA.IsValid())
					{
						desc.sample_count = required_sample_count;
						desc.mip_levels = 1;
						desc.bind_flags = BindFlag::RENDER_TARGET;
						desc.misc_flags = ResourceMiscFlag::TRANSIENT_ATTACHMENT;
						desc.layout = ResourceState::RENDERTARGET;
						desc.format = FORMAT_rendertargetEnvprobe;
						device->CreateTexture(&desc, nullptr, &envrenderingColorBuffer_MSAA);
						device->SetName(&envrenderingColorBuffer_MSAA, "envrenderingColorBuffer_MSAA");
						render_textures[id_color_msaa.raw] = envrenderingColorBuffer_MSAA;

						std::string info;
						info += "Created envprobe render target for request";
						info += "\n\tResolution = " + std::to_string(desc.width) + " * " + std::to_string(desc.height) + " * 6";
						info += "\n\tSample Count = " + std::to_string(desc.sample_count);
						info += "\n\tMip Levels = " + std::to_string(desc.mip_levels);
						info += "\n\tFormat = ";
						info += GetFormatString(desc.format);
						info += "\n\tMemory = " + helper::GetMemorySizeText(ComputeTextureMemorySizeInBytes(desc)) + "\n";
						backlog::post(info);
					}
				}
			}

			if (probe.IsMSAA())
			{
				const RenderPassImage rp[] = {
					RenderPassImage::RenderTarget(
						&envrenderingColorBuffer_MSAA,
						RenderPassImage::LoadOp::CLEAR,
						RenderPassImage::StoreOp::DONTCARE,
						ResourceState::RENDERTARGET,
						ResourceState::RENDERTARGET
					),
					RenderPassImage::Resolve(
						&envrenderingColorBuffer,
						ResourceState::SHADER_RESOURCE,
						ResourceState::SHADER_RESOURCE,
						0
					),
					RenderPassImage::DepthStencil(
						&envrenderingDepthBuffer,
						RenderPassImage::LoadOp::CLEAR,
						RenderPassImage::StoreOp::STORE,
						ResourceState::SHADER_RESOURCE,
						ResourceState::DEPTHSTENCIL,
						ResourceState::SHADER_RESOURCE
					),
				};
				device->RenderPassBegin(rp, arraysize(rp), cmd);
			}
			else
			{
				const RenderPassImage rp[] = {
					RenderPassImage::DepthStencil(
						&envrenderingDepthBuffer,
						RenderPassImage::LoadOp::CLEAR,
						RenderPassImage::StoreOp::STORE,
						ResourceState::SHADER_RESOURCE,
						ResourceState::DEPTHSTENCIL,
						ResourceState::SHADER_RESOURCE
					),
					RenderPassImage::RenderTarget(
						&envrenderingColorBuffer,
						RenderPassImage::LoadOp::CLEAR,
						RenderPassImage::StoreOp::STORE,
						ResourceState::SHADER_RESOURCE,
						ResourceState::SHADER_RESOURCE
					)
				};
				device->RenderPassBegin(rp, arraysize(rp), cmd);
			}

			// Scene will only be rendered if this is a real probe entity:
			if (probeAABB.layerMask & vis.layerMask)
			{
				Sphere culler(probe.position, z_far_p);

				renderQueue.init();
				for (size_t i = 0; i < scene_Gdetails->aabbRenderables.size(); ++i)
				{
					const AABB& aabb = scene_Gdetails->aabbRenderables[i];
					if ((aabb.layerMask & vis.layerMask) && (aabb.layerMask & probeAABB.layerMask) && culler.intersects(aabb))
					{
						const GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[i];
						if (renderable.IsRenderable() && !renderable.IsInvisibleInReflections())
						{
							uint8_t camera_mask = 0;
							for (uint32_t camera_index = 0; camera_index < arraysize(cameras); ++camera_index)
							{
								if (cameras[camera_index].frustum.CheckBoxFast(aabb))
								{
									camera_mask |= 1 << camera_index;
								}
							}
							if (camera_mask == 0)
								continue;

							renderQueue.add(renderable.geometry->geometryIndex, uint32_t(i), 0, renderable.sortBits, camera_mask);
						}
					}
				}

				if (!renderQueue.empty())
				{
					RenderMeshes(vis, renderQueue, RENDERPASS_ENVMAPCAPTURE, GMaterialComponent::FILTER_ALL, cmd, 0, arraysize(cameras));
				}
			}

			// sky
			{
				if (environment->skyMap.IsValid())
				{
					device->BindPipelineState(&PSO_sky[SKY_RENDERING_ENVMAPCAPTURE_STATIC], cmd);
				}
				else
				{
					device->BindPipelineState(&PSO_sky[SKY_RENDERING_ENVMAPCAPTURE_DYNAMIC], cmd);
				}

				device->DrawInstanced(240, 6, 0, 0, cmd); // 6 instances so it will be replicated for every cubemap face
			}

			device->RenderPassEnd(cmd);

			// Compute Aerial Perspective for environment map
			if (environment->IsRealisticSky() && environment->IsRealisticSkyAerialPerspective() && (probeAABB.layerMask & vis.layerMask))
			{
				if (probe.IsMSAA())
				{
					device->EventBegin("Aerial Perspective Capture [MSAA]", cmd);
					device->BindComputeShader(&shaders[CSTYPE_POSTPROCESS_AERIALPERSPECTIVE_CAPTURE_MSAA], cmd);
				}
				else
				{
					device->EventBegin("Aerial Perspective Capture", cmd);
					device->BindComputeShader(&shaders[CSTYPE_POSTPROCESS_AERIALPERSPECTIVE_CAPTURE], cmd);
				}

				device->BindResource(&envrenderingDepthBuffer, 0, cmd);

				TextureDesc desc = envrenderingColorBuffer.GetDesc();

				AerialPerspectiveCapturePushConstants push;
				push.resolution.x = desc.width;
				push.resolution.y = desc.height;
				push.resolution_rcp.x = 1.0f / push.resolution.x;
				push.resolution_rcp.y = 1.0f / push.resolution.y;
				push.texture_input = device->GetDescriptorIndex(&envrenderingColorBuffer, SubresourceType::SRV);
				push.texture_output = device->GetDescriptorIndex(&envrenderingColorBuffer, SubresourceType::UAV);

				device->PushConstants(&push, sizeof(push), cmd);

				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&envrenderingColorBuffer, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}

				device->Dispatch(
					(desc.width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
					(desc.height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
					6,
					cmd);

				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&envrenderingColorBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}

				device->EventEnd(cmd);
			}

			// Compute Volumetric Clouds for environment map
			if (environment->IsVolumetricClouds() && (probeAABB.layerMask & vis.layerMask))
			{
				if (probe.IsMSAA())
				{
					device->EventBegin("Volumetric Cloud Rendering Capture [MSAA]", cmd);
					device->BindComputeShader(&shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_RENDER_CAPTURE_MSAA], cmd);
				}
				else
				{
					device->EventBegin("Volumetric Cloud Rendering Capture", cmd);
					device->BindComputeShader(&shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_RENDER_CAPTURE], cmd);
				}

				device->BindResource(&envrenderingDepthBuffer, 5, cmd);

				device->BindResource(&textureShapeNoise, 0, cmd);
				device->BindResource(&textureDetailNoise, 1, cmd);
				device->BindResource(&textureCurlNoise, 2, cmd);

				if (environment->volumetricCloudsWeatherMapFirst.IsValid())
				{
					device->BindResource(&environment->volumetricCloudsWeatherMapFirst.GetTexture(), 3, cmd);
				}
				else
				{
					device->BindResource(&textureEnvMap, 3, cmd);
				}

				if (environment->volumetricCloudsWeatherMapSecond.IsValid())
				{
					device->BindResource(&environment->volumetricCloudsWeatherMapSecond.GetTexture(), 4, cmd);
				}
				else
				{
					device->BindResource(&textureEnvMap, 4, cmd);
				}

				TextureDesc desc = envrenderingColorBuffer.GetDesc();

				VolumetricCloudCapturePushConstants push;
				push.resolution.x = desc.width;
				push.resolution.y = desc.height;
				push.resolution_rcp.x = 1.0f / push.resolution.x;
				push.resolution_rcp.y = 1.0f / push.resolution.y;
				push.texture_input = device->GetDescriptorIndex(&envrenderingColorBuffer, SubresourceType::SRV);
				push.texture_output = device->GetDescriptorIndex(&envrenderingColorBuffer, SubresourceType::UAV);

				if (probe.IsRealTime())
				{
					push.maxStepCount = 32;
					push.LODMin = 3;
					push.shadowSampleCount = 3;
					push.groundContributionSampleCount = 2;
				}
				else
				{
					// Use same parameters as current view
					const EnvironmentComponent::VolumetricCloudParameters& volumetric_cloud_parameters = environment->GetVolumetricCloudParameters();
					push.maxStepCount = volumetric_cloud_parameters.maxStepCount;
					push.LODMin = volumetric_cloud_parameters.LODMin;
					push.shadowSampleCount = volumetric_cloud_parameters.shadowSampleCount;
					push.groundContributionSampleCount = volumetric_cloud_parameters.groundContributionSampleCount;
				}

				device->PushConstants(&push, sizeof(push), cmd);

				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&envrenderingColorBuffer, ResourceState::SHADER_RESOURCE, ResourceState::UNORDERED_ACCESS),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}

				device->Dispatch(
					(desc.width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
					(desc.height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
					6,
					cmd);

				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&envrenderingColorBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}

				device->EventEnd(cmd);
			}

			GenerateMipChain(envrenderingColorBuffer, MIPGENFILTER_LINEAR, cmd);

			// Filter the enviroment map mip chain according to BRDF:
			//	A bit similar to MIP chain generation, but its input is the MIP-mapped texture,
			//	and we generatethe filtered MIPs from bottom to top.
			device->EventBegin("FilterEnvMap", cmd);
			{
				// Copy over whole:
				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&envrenderingColorBuffer, envrenderingColorBuffer.desc.layout, ResourceState::COPY_SRC),
						GPUBarrier::Image(&envrenderingColorBuffer_Filtered, envrenderingColorBuffer_Filtered.desc.layout, ResourceState::COPY_DST),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}
				device->CopyResource(&envrenderingColorBuffer_Filtered, &envrenderingColorBuffer, cmd);
				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&envrenderingColorBuffer, ResourceState::COPY_SRC, envrenderingColorBuffer.desc.layout),
						GPUBarrier::Image(&envrenderingColorBuffer_Filtered, ResourceState::COPY_DST, ResourceState::UNORDERED_ACCESS),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}

				TextureDesc desc = envrenderingColorBuffer_Filtered.GetDesc();

				device->BindComputeShader(&shaders[CSTYPE_FILTERENVMAP], cmd);

				int mip_start = desc.mip_levels - 1;
				desc.width = std::max(1u, desc.width >> mip_start);
				desc.height = std::max(1u, desc.height >> mip_start);
				for (int i = mip_start; i > 0; --i)
				{
					FilterEnvmapPushConstants push;
					push.filterResolution.x = desc.width;
					push.filterResolution.y = desc.height;
					push.filterResolution_rcp.x = 1.0f / push.filterResolution.x;
					push.filterResolution_rcp.y = 1.0f / push.filterResolution.y;
					push.filterRoughness = (float)i / (float)(desc.mip_levels - 1);
					if (probeAABB.layerMask & vis.layerMask)
					{
						// real probe:
						if (probe.IsRealTime())
						{
							push.filterRayCount = 1024;
						}
						else
						{
							push.filterRayCount = 8192;
						}
					}
					else
					{
						// dummy probe, reduced ray count:
						push.filterRayCount = 64;
					}
					push.texture_input = device->GetDescriptorIndex(&envrenderingColorBuffer, SubresourceType::SRV);
					push.texture_output = device->GetDescriptorIndex(&envrenderingColorBuffer_Filtered, SubresourceType::UAV, i);
					device->PushConstants(&push, sizeof(push), cmd);

					device->Dispatch(
						(desc.width + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE,
						(desc.height + GENERATEMIPCHAIN_2D_BLOCK_SIZE - 1) / GENERATEMIPCHAIN_2D_BLOCK_SIZE,
						6,
						cmd
					);

					desc.width *= 2;
					desc.height *= 2;
				}

				{
					GPUBarrier barriers[] = {
						GPUBarrier::Image(&envrenderingColorBuffer_Filtered, ResourceState::UNORDERED_ACCESS, envrenderingColorBuffer_Filtered.desc.layout),
					};
					device->Barrier(barriers, arraysize(barriers), cmd);
				}
			}
			device->EventEnd(cmd);

			// Finally, the complete envmap is block compressed into the probe's texture:
			BlockCompress(envrenderingColorBuffer_Filtered, probe.texture, cmd);
			};

		if (vis.visibleEnvProbes.size() == 0)
		{
			// In this case, there are no probes, so the sky will be rendered to first envmap:
			const GProbeComponent& probe = scene_Gdetails->globalDynamicProbe;

			geometrics::AABB probe_aabb;
			probe_aabb.layerMask = 0;
			if (probe.texture.IsValid())
			{
				renderProbe(probe, probe_aabb);
			}
		}
		else
		{
			bool rendered_anything = false;
			for (size_t i = 0; i < vis.visibleEnvProbes.size(); ++i)
			{
				uint32_t probe_index = vis.visibleEnvProbes[i];
				const GProbeComponent& probe = *scene_Gdetails->probeComponents[probe_index];
				const AABB& probe_aabb = scene_Gdetails->aabbProbes[probe_index];

				if ((probe_aabb.layerMask & vis.layerMask) && probe.renderDirty && probe.texture.IsValid())
				{
					probe.renderDirty = false;
					renderProbe(probe, probe_aabb);
					rendered_anything = true;
				}
			}

			// Reset SkyAtmosphere SkyViewLut after usage:
			if (rendered_anything)
			{
				CameraCB cb;
				cb.Init();
				cb.cameras[0].position = vis.camera->GetWorldEye();
				device->BindDynamicConstantBuffer(cb, CBSLOT_RENDERER_CAMERA, cmd);

				ComputeSkyAtmosphereSkyViewLut(cmd);
			}
		}

		profiler::EndRange(range);
		device->EventEnd(cmd); // EnvironmentProbe Refresh
	}
	void GRenderPath3DDetails::RefreshWetmaps(const Visibility& vis, CommandList cmd)
	{
		return; // this will be useful for wetmap simulation for rainny weather...

		device->EventBegin("RefreshWetmaps", cmd);

		BindCommonResources(cmd);
		device->BindComputeShader(&shaders[CSTYPE_WETMAP_UPDATE], cmd);

		WetmapPush push = {};
		push.wet_amount = 1.f;

		// Note: every object wetmap is updated, not just visible
		for (uint32_t i = 0, n = (uint32_t)vis.visibleRenderables_Mesh.size(); i < n; ++i)
		{
			push.instanceIndex = vis.visibleRenderables_Mesh[i];
			GRenderableComponent& renderable = *scene_Gdetails->renderableComponents[push.instanceIndex];

			assert(renderable.GetRenderableType() == RenderableType::MESH_RENDERABLE);

			GGeometryComponent& geometry = *renderable.geometry;

			std::vector<Entity> materials(renderable.GetNumParts());
			assert(geometry.GetNumParts() == renderable.bufferEffects.size());
			renderable.GetMaterials(materials.data());
			for (size_t part_index = 0, n = renderable.bufferEffects.size(); part_index < n; ++part_index)
			{
				GPrimEffectBuffers& prim_effect_buffers = renderable.bufferEffects[part_index];
				GMaterialComponent& material = *renderable.materials[part_index];
				if (!material.IsWetmapEnabled() && prim_effect_buffers.wetmapBuffer.IsValid())
					continue;
				uint32_t vertex_count = uint32_t(prim_effect_buffers.wetmapBuffer.desc.size
					/ GetFormatStride(prim_effect_buffers.wetmapBuffer.desc.format));
				push.wetmap = device->GetDescriptorIndex(&prim_effect_buffers.wetmapBuffer, SubresourceType::UAV);
				if (push.wetmap < 0)
					continue;

				push.subsetIndex = part_index;

				device->PushConstants(&push, sizeof(push), cmd);
				device->Dispatch((vertex_count + 63u) / 64u, 1, 1, cmd);
			}
		}

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::DrawSun(CommandList cmd)
	{
		device->EventBegin("DrawSun", cmd);

		device->BindPipelineState(&PSO_sky[SKY_RENDERING_SUN], cmd);

		BindCommonResources(cmd);

		device->Draw(3, 0, cmd);

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::DrawSky(CommandList cmd)
	{
		device->EventBegin("DrawSky", cmd);
		
		if (scene_Gdetails->environment->skyMap.IsValid())
		{
			device->BindPipelineState(&PSO_sky[SKY_RENDERING_STATIC], cmd);
		}
		else
		{
			device->BindPipelineState(&PSO_sky[SKY_RENDERING_DYNAMIC], cmd);
		}

		BindCommonResources(cmd);

		device->Draw(3, 0, cmd);

		device->EventEnd(cmd);
	}

}
