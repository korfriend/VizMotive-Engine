#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::RenderPostprocessChain(CommandList cmd)
	{
		if (skipPostprocess)
		{
			lastPostprocessRT = &rtMain;
			return;
		}

		BindCommonResources(cmd);
		BindCameraCB(*camera, cameraPrevious, cameraReflection, cmd);

		const Texture* rt_first = nullptr; // not ping-ponged with read / write
		const Texture* rt_read = &rtMain;
		const Texture* rt_write = &rtPostprocess;

		// rtPostprocess aliasing transition:
		{
			GPUBarrier barriers[] = {
				GPUBarrier::Aliasing(&rtPrimitiveID_1, &rtPostprocess),
				GPUBarrier::Image(&rtPostprocess, rtPostprocess.desc.layout, ResourceState::UNORDERED_ACCESS),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
			device->ClearUAV(&rtPostprocess, 0, cmd);
			device->Barrier(GPUBarrier::Image(&rtPostprocess, ResourceState::UNORDERED_ACCESS, rtPostprocess.desc.layout), cmd);
		}

		// 1.) HDR post process chain
		{
			// TODO: TAA, FSR, DOF, MBlur
			if (renderer::isFSREnabled && fsr2Resources.IsValid())
			{
				//renderer::Postprocess_FSR2(
				//	fsr2Resources,
				//	*camera,
				//	rtFSR[1],
				//	*rt_read,
				//	depthBuffer_Copy,
				//	rtVelocity,
				//	rtFSR[0],
				//	cmd,
				//	scene->dt,
				//	getFSR2Sharpness()
				//);
				//
				//// rebind these, because FSR2 binds other things to those constant buffers:
				//renderer::BindCameraCB(
				//	*camera,
				//	camera_previous,
				//	camera_reflection,
				//	cmd
				//);
				//renderer::BindCommonResources(cmd);
				//
				//rt_read = &rtFSR[0];
				//rt_write = &rtFSR[1];
			}
			else if (renderer::isTemporalAAEnabled && !renderer::isTemporalAADebugEnabled && temporalAAResources.IsValid() && !camera->IsSlicer())
			{
				Postprocess_TemporalAA(
					temporalAAResources,
					*rt_read,
					cmd
				);
				rt_first = temporalAAResources.GetCurrent();
			}

			//if (getDepthOfFieldEnabled() && camera->aperture_size > 0.001f && getDepthOfFieldStrength() > 0.001f && depthoffieldResources.IsValid())
			//{
			//	renderer::Postprocess_DepthOfField(
			//		depthoffieldResources,
			//		rt_first == nullptr ? *rt_read : *rt_first,
			//		*rt_write,
			//		cmd,
			//		getDepthOfFieldStrength()
			//	);
			//
			//	rt_first = nullptr;
			//	std::swap(rt_read, rt_write);
			//}
			//
			//if (getMotionBlurEnabled() && getMotionBlurStrength() > 0 && motionblurResources.IsValid())
			//{
			//	renderer::Postprocess_MotionBlur(
			//		motionblurResources,
			//		rt_first == nullptr ? *rt_read : *rt_first,
			//		*rt_write,
			//		cmd,
			//		getMotionBlurStrength()
			//	);
			//
			//	rt_first = nullptr;
			//	std::swap(rt_read, rt_write);
			//}
		}

		// 2.) Tone mapping HDR -> LDR
		if (renderer::isTonemapping)
		{
			// Bloom and eye adaption is not part of post process "chain",
			//	because they will be applied to the screen in tonemap

			//if (getEyeAdaptionEnabled())
			//{
			//	renderer::ComputeLuminance(
			//		luminanceResources,
			//		rt_first == nullptr ? *rt_read : *rt_first,
			//		cmd,
			//		getEyeAdaptionRate(),
			//		getEyeAdaptionKey()
			//	);
			//}
			//if (getBloomEnabled())
			//{
			//	renderer::ComputeBloom(
			//		bloomResources,
			//		rt_first == nullptr ? *rt_read : *rt_first,
			//		cmd,
			//		getBloomThreshold(),
			//		getExposure(),
			//		getEyeAdaptionEnabled() ? &luminanceResources.luminance : nullptr
			//	);
			//}

			Postprocess_Tonemap(
				rt_first == nullptr ? *rt_read : *rt_first,
				*rt_write,
				cmd,
				camera->GetSensorExposure(),
				camera->GetSensorBrightness(),
				camera->GetSensorContrast(),
				camera->GetSensorSaturation(),
				false, //getDitherEnabled(),
				isColorGradingEnabled ? &scene_Gdetails->environment->colorGradingMap.GetTexture() : nullptr,
				&rtParticleDistortion,
				camera->IsSensorEyeAdaptationEnabled() ? &luminanceResources.luminance : nullptr,
				camera->IsSensorBloomEnabled() ? &bloomResources.texture_bloom : nullptr,
				colorspace,
				tonemap,
				&distortion_overlay,
				camera->GetSensorHdrCalibration()
			);

			rt_first = nullptr;
			std::swap(rt_read, rt_write);
		}

		// 3.) LDR post process chain
		{
			/*
			if (getSharpenFilterEnabled())
			{
				renderer::Postprocess_Sharpen(*rt_read, *rt_write, cmd, getSharpenFilterAmount());

				std::swap(rt_read, rt_write);
			}

			if (getFXAAEnabled())
			{
				renderer::Postprocess_FXAA(*rt_read, *rt_write, cmd);

				std::swap(rt_read, rt_write);
			}

			if (getChromaticAberrationEnabled())
			{
				renderer::Postprocess_Chromatic_Aberration(*rt_read, *rt_write, cmd, getChromaticAberrationAmount());

				std::swap(rt_read, rt_write);
			}
			/**/

			lastPostprocessRT = rt_read;

			// GUI Background blurring:
			//{
			//	auto range = profiler::BeginRangeGPU("GUI Background Blur", cmd);
			//	device->EventBegin("GUI Background Blur", cmd);
			//	renderer::Postprocess_Downsample4x(*rt_read, rtGUIBlurredBackground[0], cmd);
			//	renderer::Postprocess_Downsample4x(rtGUIBlurredBackground[0], rtGUIBlurredBackground[2], cmd);
			//	renderer::Postprocess_Blur_Gaussian(rtGUIBlurredBackground[2], rtGUIBlurredBackground[1], rtGUIBlurredBackground[2], cmd, -1, -1, true);
			//	device->EventEnd(cmd);
			//	profiler::EndRange(range);
			//}
			//
			//if (rtFSR[0].IsValid() && getFSREnabled())
			//{
			//	renderer::Postprocess_FSR(*rt_read, rtFSR[1], rtFSR[0], cmd, getFSRSharpness());
			//	lastPostprocessRT = &rtFSR[0];
			//}
		}
	}

	void GRenderPath3DDetails::Postprocess_Tonemap(
		const Texture& input,
		const Texture& output,
		CommandList cmd,
		float exposure,
		float brightness,
		float contrast,
		float saturation,
		bool dither,
		const Texture* texture_colorgradinglut,
		const Texture* texture_distortion,
		const GPUBuffer* buffer_luminance,
		const Texture* texture_bloom,
		ColorSpace display_colorspace,
		Tonemap tonemap,
		const Texture* texture_distortion_overlay,
		float hdr_calibration
	)
	{
		if (!input.IsValid() || !output.IsValid())
		{
			assert(0);
			return;
		}

		device->EventBegin("Postprocess_Tonemap", cmd);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&output, output.desc.layout, ResourceState::UNORDERED_ACCESS),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->ClearUAV(&output, 0, cmd);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Memory(&output),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->BindComputeShader(&shaders[CSTYPE_POSTPROCESS_TONEMAP], cmd);

		const TextureDesc& desc = output.GetDesc();

		assert(texture_colorgradinglut == nullptr || texture_colorgradinglut->desc.type == TextureDesc::Type::TEXTURE_3D); // This must be a 3D lut

		XMHALF4 exposure_brightness_contrast_saturation = XMHALF4(exposure, brightness, contrast, saturation);

		PushConstantsTonemap tonemap_push = {};
		tonemap_push.resolution_rcp.x = 1.0f / desc.width;
		tonemap_push.resolution_rcp.y = 1.0f / desc.height;
		tonemap_push.exposure_brightness_contrast_saturation.x = uint(exposure_brightness_contrast_saturation.v);
		tonemap_push.exposure_brightness_contrast_saturation.y = uint(exposure_brightness_contrast_saturation.v >> 32ull);
		tonemap_push.flags_hdrcalibration = 0;
		if (dither)
		{
			tonemap_push.flags_hdrcalibration |= TONEMAP_FLAG_DITHER;
		}
		if (tonemap == Tonemap::ACES)
		{
			tonemap_push.flags_hdrcalibration |= TONEMAP_FLAG_ACES;
		}
		if (display_colorspace == ColorSpace::SRGB)
		{
			tonemap_push.flags_hdrcalibration |= TONEMAP_FLAG_SRGB;
		}
		tonemap_push.flags_hdrcalibration |= XMConvertFloatToHalf(hdr_calibration) << 16u;
		tonemap_push.texture_input = device->GetDescriptorIndex(&input, SubresourceType::SRV);
		tonemap_push.buffer_input_luminance = device->GetDescriptorIndex(buffer_luminance, SubresourceType::SRV);
		tonemap_push.texture_input_distortion = device->GetDescriptorIndex(texture_distortion, SubresourceType::SRV);
		tonemap_push.texture_input_distortion_overlay = device->GetDescriptorIndex(texture_distortion_overlay, SubresourceType::SRV);
		tonemap_push.texture_colorgrade_lookuptable = device->GetDescriptorIndex(texture_colorgradinglut, SubresourceType::SRV);
		tonemap_push.texture_bloom = device->GetDescriptorIndex(texture_bloom, SubresourceType::SRV);
		tonemap_push.texture_output = device->GetDescriptorIndex(&output, SubresourceType::UAV);
		device->PushConstants(&tonemap_push, sizeof(tonemap_push), cmd);

		device->Dispatch(
			(desc.width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
			(desc.height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
			1,
			cmd
		);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&output, ResourceState::UNORDERED_ACCESS, output.desc.layout),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}


		device->EventEnd(cmd);
	}


	void GRenderPath3DDetails::Postprocess_Downsample4x(
		const Texture& input,
		const Texture& output,
		CommandList cmd
	)
	{
		device->EventBegin("Postprocess_Downsample4x", cmd);

		device->BindComputeShader(&shaders[CSTYPE_POSTPROCESS_DOWNSAMPLE4X], cmd);

		const TextureDesc& desc = output.GetDesc();

		PostProcess postprocess;
		postprocess.resolution.x = desc.width;
		postprocess.resolution.y = desc.height;
		postprocess.resolution_rcp.x = 1.0f / postprocess.resolution.x;
		postprocess.resolution_rcp.y = 1.0f / postprocess.resolution.y;
		device->PushConstants(&postprocess, sizeof(postprocess), cmd);

		device->BindResource(&input, 0, cmd);

		const GPUResource* uavs[] = {
			&output,
		};
		device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&output, output.desc.layout, ResourceState::UNORDERED_ACCESS),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->ClearUAV(&output, 0, cmd);
		device->Barrier(GPUBarrier::Memory(&output), cmd);

		device->Dispatch(
			(desc.width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
			(desc.height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
			1,
			cmd
		);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&output, ResourceState::UNORDERED_ACCESS, output.desc.layout),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::Postprocess_Blur_Gaussian(
		const Texture& input,
		const Texture& temp,
		const Texture& output,
		CommandList cmd,
		int mip_src,
		int mip_dst,
		bool wide
	)
	{
		device->EventBegin("Postprocess_Blur_Gaussian", cmd);

		SHADERTYPE cs = CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT4;
		switch (output.GetDesc().format)
		{
		case Format::R16_UNORM:
		case Format::R8_UNORM:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_UNORM1 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_UNORM1;
			break;
		case Format::R16_FLOAT:
		case Format::R32_FLOAT:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT1 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT1;
			break;
		case Format::R16G16B16A16_UNORM:
		case Format::R8G8B8A8_UNORM:
		case Format::B8G8R8A8_UNORM:
		case Format::R10G10B10A2_UNORM:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_UNORM4 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_UNORM4;
			break;
		case Format::R11G11B10_FLOAT:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT3 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT3;
			break;
		case Format::R16G16B16A16_FLOAT:
		case Format::R32G32B32A32_FLOAT:
			cs = wide ? CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT4 : CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT4;
			break;
		default:
			assert(0); // implement format!
			break;
		}
		device->BindComputeShader(&shaders[cs], cmd);

		// Horizontal:
		{
			const TextureDesc& desc = temp.GetDesc();

			PostProcess postprocess;
			postprocess.resolution.x = desc.width;
			postprocess.resolution.y = desc.height;
			if (mip_dst > 0)
			{
				postprocess.resolution.x >>= mip_dst;
				postprocess.resolution.y >>= mip_dst;
			}
			postprocess.resolution_rcp.x = 1.0f / postprocess.resolution.x;
			postprocess.resolution_rcp.y = 1.0f / postprocess.resolution.y;
			postprocess.params0.x = 1;
			postprocess.params0.y = 0;
			device->PushConstants(&postprocess, sizeof(postprocess), cmd);

			device->BindResource(&input, 0, cmd, mip_src);
			device->BindUAV(&temp, 0, cmd, mip_dst);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&temp, temp.desc.layout, ResourceState::UNORDERED_ACCESS, mip_dst),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->Dispatch(
				(postprocess.resolution.x + POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT - 1) / POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT,
				postprocess.resolution.y,
				1,
				cmd
			);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&temp, ResourceState::UNORDERED_ACCESS, temp.desc.layout, mip_dst),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

		}

		// Vertical:
		{
			const TextureDesc& desc = output.GetDesc();

			PostProcess postprocess;
			postprocess.resolution.x = desc.width;
			postprocess.resolution.y = desc.height;
			if (mip_dst > 0)
			{
				postprocess.resolution.x >>= mip_dst;
				postprocess.resolution.y >>= mip_dst;
			}
			postprocess.resolution_rcp.x = 1.0f / postprocess.resolution.x;
			postprocess.resolution_rcp.y = 1.0f / postprocess.resolution.y;
			postprocess.params0.x = 0;
			postprocess.params0.y = 1;
			device->PushConstants(&postprocess, sizeof(postprocess), cmd);

			device->BindResource(&temp, 0, cmd, mip_dst); // <- also mip_dst because it's second pass!
			device->BindUAV(&output, 0, cmd, mip_dst);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&output, output.desc.layout, ResourceState::UNORDERED_ACCESS, mip_dst),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->Dispatch(
				postprocess.resolution.x,
				(postprocess.resolution.y + POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT - 1) / POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT,
				1,
				cmd
			);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Image(&output, ResourceState::UNORDERED_ACCESS, output.desc.layout, mip_dst),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

		}

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::Postprocess_TemporalAA(
		const TemporalAAResources& res,
		const Texture& input,
		CommandList cmd
	)
	{
		device->EventBegin("Postprocess_TemporalAA", cmd);
		auto range = profiler::BeginRangeGPU("Temporal AA Resolve", &cmd);
		const bool first_frame = res.frame == 0;
		res.frame++;

		if (first_frame)
		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(&res.textureTemporal[0], res.textureTemporal[0].desc.layout, ResourceState::UNORDERED_ACCESS),
				GPUBarrier::Image(&res.textureTemporal[1], res.textureTemporal[1].desc.layout, ResourceState::UNORDERED_ACCESS),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);

			device->ClearUAV(&res.textureTemporal[0], 0, cmd);
			device->ClearUAV(&res.textureTemporal[1], 0, cmd);

			std::swap(barriers[0].image.layout_before, barriers[0].image.layout_after);
			std::swap(barriers[1].image.layout_before, barriers[1].image.layout_after);
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->BindComputeShader(&shaders[CSTYPE_POSTPROCESS_TEMPORALAA], cmd);

		device->BindResource(&input, 0, cmd); // input_current

		if (first_frame)
		{
			device->BindResource(&input, 1, cmd); // input_history
		}
		else
		{
			device->BindResource(res.GetHistory(), 1, cmd); // input_history
		}

		const TextureDesc& desc = res.textureTemporal[0].GetDesc();

		PostProcess postprocess = {};
		postprocess.resolution.x = desc.width;
		postprocess.resolution.y = desc.height;
		postprocess.resolution_rcp.x = 1.0f / postprocess.resolution.x;
		postprocess.resolution_rcp.y = 1.0f / postprocess.resolution.y;
		postprocess.params0.x = first_frame ? 1.0f : 0.0f;
		device->PushConstants(&postprocess, sizeof(postprocess), cmd);

		const Texture* output = res.GetCurrent();

		const GPUResource* uavs[] = {
			output,
		};
		device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(output, output->GetDesc().layout, ResourceState::UNORDERED_ACCESS),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		device->Dispatch(
			(desc.width + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
			(desc.height + POSTPROCESS_BLOCKSIZE - 1) / POSTPROCESS_BLOCKSIZE,
			1,
			cmd
		);

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Image(output, ResourceState::UNORDERED_ACCESS, output->GetDesc().layout),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
}