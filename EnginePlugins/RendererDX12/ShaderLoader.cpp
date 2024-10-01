#include "ShaderLoader.h"

namespace vz::graphics
{
	bool LoadShader(
		ShaderStage stage,
		Shader& shader,
		const std::string& filename,
		ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines
	)
	{
		std::string shaderbinaryfilename = SHADERPATH + filename;

		if (!permutation_defines.empty())
		{
			std::string ext = wi::helper::GetExtensionFromFileName(shaderbinaryfilename);
			shaderbinaryfilename = wi::helper::RemoveExtension(shaderbinaryfilename);
			for (auto& def : permutation_defines)
			{
				shaderbinaryfilename += "_" + def;
			}
			shaderbinaryfilename += "." + ext;
		}

		if (device != nullptr)
		{
#ifdef SHADERDUMP_ENABLED
			// Loading shader from precompiled dump:
			auto it = wiShaderDump::shaderdump.find(shaderbinaryfilename);
			if (it != wiShaderDump::shaderdump.end())
			{
				return device->CreateShader(stage, it->second.data, it->second.size, &shader);
			}
			else
			{
				wi::backlog::post("shader dump doesn't contain shader: " + shaderbinaryfilename, wi::backlog::LogLevel::Error);
			}
#endif // SHADERDUMP_ENABLED
		}

		wi::shadercompiler::RegisterShader(shaderbinaryfilename);

		if (wi::shadercompiler::IsShaderOutdated(shaderbinaryfilename))
		{
			wi::shadercompiler::CompilerInput input;
			input.format = device->GetShaderFormat();
			input.stage = stage;
			input.minshadermodel = minshadermodel;
			input.defines = permutation_defines;

			std::string sourcedir = SHADERSOURCEPATH;
			wi::helper::MakePathAbsolute(sourcedir);
			input.include_directories.push_back(sourcedir);
			input.include_directories.push_back(sourcedir + wi::helper::GetDirectoryFromPath(filename));
			input.shadersourcefilename = wi::helper::ReplaceExtension(sourcedir + filename, "hlsl");

			wi::shadercompiler::CompilerOutput output;
			wi::shadercompiler::Compile(input, output);

			if (output.IsValid())
			{
				wi::shadercompiler::SaveShaderAndMetadata(shaderbinaryfilename, output);

				if (!output.error_message.empty())
				{
					wi::backlog::post(output.error_message, wi::backlog::LogLevel::Warning);
				}
				wi::backlog::post("shader compiled: " + shaderbinaryfilename);
				return device->CreateShader(stage, output.shaderdata, output.shadersize, &shader);
			}
			else
			{
				wi::backlog::post("shader compile FAILED: " + shaderbinaryfilename + "\n" + output.error_message, wi::backlog::LogLevel::Error);
				SHADER_ERRORS.fetch_add(1);
			}
		}

		if (device != nullptr)
		{
			wi::vector<uint8_t> buffer;
			if (wi::helper::FileRead(shaderbinaryfilename, buffer))
			{
				bool success = device->CreateShader(stage, buffer.data(), buffer.size(), &shader);
				if (success)
				{
					device->SetName(&shader, shaderbinaryfilename.c_str());
				}
				return success;
			}
			else
			{
				SHADER_MISSING.fetch_add(1);
			}
		}

		return false;
	}


	void LoadShaders()
	{
		wi::jobsystem::Wait(raytracing_ctx);
		raytracing_ctx.priority = wi::jobsystem::Priority::Low;

		wi::jobsystem::Wait(objectps_ctx);
		objectps_ctx.priority = wi::jobsystem::Priority::Low;

		wi::jobsystem::context ctx;

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, shaders[VSTYPE_OBJECT_DEBUG], "objectVS_debug.cso");
			});

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, shaders[VSTYPE_OBJECT_COMMON], "objectVS_common.cso");
			});

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, shaders[VSTYPE_OBJECT_PREPASS], "objectVS_prepass.cso");
			});

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, shaders[VSTYPE_OBJECT_PREPASS_ALPHATEST], "objectVS_prepass_alphatest.cso");
			});

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, shaders[VSTYPE_OBJECT_SIMPLE], "objectVS_simple.cso");
			});

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			inputLayouts[ILTYPE_VERTEXCOLOR].elements =
			{
				{ "POSITION", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
				{ "TEXCOORD", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
			};
			LoadShader(ShaderStage::VS, shaders[VSTYPE_VERTEXCOLOR], "vertexcolorVS.cso");
			});

		inputLayouts[ILTYPE_POSITION].elements =
		{
			{ "POSITION", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
		};

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_OBJECT_COMMON_TESSELLATION], "objectVS_common_tessellation.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_OBJECT_PREPASS_TESSELLATION], "objectVS_prepass_tessellation.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_OBJECT_PREPASS_ALPHATEST_TESSELLATION], "objectVS_prepass_alphatest_tessellation.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_OBJECT_SIMPLE_TESSELLATION], "objectVS_simple_tessellation.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_IMPOSTOR], "impostorVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_VOLUMETRICLIGHT_DIRECTIONAL], "volumetriclight_directionalVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_VOLUMETRICLIGHT_POINT], "volumetriclight_pointVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_VOLUMETRICLIGHT_SPOT], "volumetriclight_spotVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_LIGHTVISUALIZER_SPOTLIGHT], "vSpotLightVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_LIGHTVISUALIZER_POINTLIGHT], "vPointLightVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_SPHERE], "sphereVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_OCCLUDEE], "occludeeVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_SKY], "skyVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_VOXELIZER], "objectVS_voxelizer.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_VOXEL], "voxelVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_FORCEFIELDVISUALIZER_POINT], "forceFieldPointVisualizerVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_FORCEFIELDVISUALIZER_PLANE], "forceFieldPlaneVisualizerVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_RAYTRACE_SCREEN], "raytrace_screenVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_POSTPROCESS], "postprocessVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_LENSFLARE], "lensFlareVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_DDGI_DEBUG], "ddgi_debugVS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_SCREEN], "screenVS.cso"); });

		if (device->CheckCapability(GraphicsDeviceCapability::RENDERTARGET_AND_VIEWPORT_ARRAYINDEX_WITHOUT_GS))
		{
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_ENVMAP], "envMapVS.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_ENVMAP_SKY], "envMap_skyVS.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_SHADOW], "shadowVS.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_SHADOW_ALPHATEST], "shadowVS_alphatest.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_SHADOW_TRANSPARENT], "shadowVS_transparent.cso"); });
		}
		else
		{
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_ENVMAP], "envMapVS_emulation.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_ENVMAP_SKY], "envMap_skyVS_emulation.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_SHADOW], "shadowVS_emulation.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_SHADOW_ALPHATEST], "shadowVS_alphatest_emulation.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_SHADOW_TRANSPARENT], "shadowVS_transparent_emulation.cso"); });

			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::GS, shaders[GSTYPE_ENVMAP_EMULATION], "envMapGS_emulation.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::GS, shaders[GSTYPE_ENVMAP_SKY_EMULATION], "envMap_skyGS_emulation.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::GS, shaders[GSTYPE_SHADOW_EMULATION], "shadowGS_emulation.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::GS, shaders[GSTYPE_SHADOW_ALPHATEST_EMULATION], "shadowGS_alphatest_emulation.cso"); });
			wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::GS, shaders[GSTYPE_SHADOW_TRANSPARENT_EMULATION], "shadowGS_transparent_emulation.cso"); });
		}

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_IMPOSTOR], "impostorPS.cso"); });

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_OBJECT_HOLOGRAM], "objectPS_hologram.cso"); });

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_OBJECT_DEBUG], "objectPS_debug.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_OBJECT_PAINTRADIUS], "objectPS_paintradius.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_OBJECT_SIMPLE], "objectPS_simple.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_OBJECT_PREPASS], "objectPS_prepass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_OBJECT_PREPASS_ALPHATEST], "objectPS_prepass_alphatest.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_OBJECT_PREPASS_DEPTHONLY], "objectPS_prepass_depthonly.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_OBJECT_PREPASS_DEPTHONLY_ALPHATEST], "objectPS_prepass_depthonly_alphatest.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_IMPOSTOR_PREPASS], "impostorPS_prepass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_IMPOSTOR_PREPASS_DEPTHONLY], "impostorPS_prepass_depthonly.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_IMPOSTOR_SIMPLE], "impostorPS_simple.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_LIGHTVISUALIZER], "lightVisualizerPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_VOLUMETRICLIGHT_DIRECTIONAL], "volumetricLight_DirectionalPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_VOLUMETRICLIGHT_POINT], "volumetricLight_PointPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_VOLUMETRICLIGHT_SPOT], "volumetricLight_SpotPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_ENVMAP], "envMapPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_ENVMAP_SKY_STATIC], "envMap_skyPS_static.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_ENVMAP_SKY_DYNAMIC], "envMap_skyPS_dynamic.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_CAPTUREIMPOSTOR], "captureImpostorPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_CUBEMAP], "cubeMapPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_VERTEXCOLOR], "vertexcolorPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_SKY_STATIC], "skyPS_static.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_SKY_DYNAMIC], "skyPS_dynamic.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_SUN], "sunPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_SHADOW_ALPHATEST], "shadowPS_alphatest.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_SHADOW_TRANSPARENT], "shadowPS_transparent.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_SHADOW_WATER], "shadowPS_water.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_VOXELIZER], "objectPS_voxelizer.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_VOXEL], "voxelPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_FORCEFIELDVISUALIZER], "forceFieldVisualizerPS.cso"); });

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_RAYTRACE_DEBUGBVH], "raytrace_debugbvhPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_DOWNSAMPLEDEPTHBUFFER], "downsampleDepthBuffer4xPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_POSTPROCESS_UPSAMPLE_BILATERAL], "upsample_bilateralPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_POSTPROCESS_OUTLINE], "outlinePS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_LENSFLARE], "lensFlarePS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_DDGI_DEBUG], "ddgi_debugPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_UPSAMPLE], "volumetricCloud_upsamplePS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_COPY_DEPTH], "copyDepthPS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_COPY_STENCIL_BIT], "copyStencilBitPS.cso"); });

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::GS, shaders[GSTYPE_VOXELIZER], "objectGS_voxelizer.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::GS, shaders[GSTYPE_VOXEL], "voxelGS.cso"); });

#ifdef PLATFORM_PS5
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::GS, shaders[GSTYPE_OBJECT_PRIMITIVEID_EMULATION], "objectGS_primitiveID_emulation.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::GS, shaders[GSTYPE_OBJECT_PRIMITIVEID_EMULATION_ALPHATEST], "objectGS_primitiveID_emulation_alphatest.cso"); });
#endif // PLATFORM_PS5

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LUMINANCE_PASS1], "luminancePass1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LUMINANCE_PASS2], "luminancePass2CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SHADINGRATECLASSIFICATION], "shadingRateClassificationCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SHADINGRATECLASSIFICATION_DEBUG], "shadingRateClassificationCS_DEBUG.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_TILEFRUSTUMS], "tileFrustumsCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LIGHTCULLING], "lightCullingCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LIGHTCULLING_DEBUG], "lightCullingCS_DEBUG.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LIGHTCULLING_ADVANCED], "lightCullingCS_ADVANCED.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LIGHTCULLING_ADVANCED_DEBUG], "lightCullingCS_ADVANCED_DEBUG.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_RESOLVEMSAADEPTHSTENCIL], "resolveMSAADepthStencilCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VXGI_OFFSETPREV], "vxgi_offsetprevCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VXGI_TEMPORAL], "vxgi_temporalCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VXGI_SDF_JUMPFLOOD], "vxgi_sdf_jumpfloodCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VXGI_RESOLVE_DIFFUSE], "vxgi_resolve_diffuseCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VXGI_RESOLVE_SPECULAR], "vxgi_resolve_specularCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SKYATMOSPHERE_TRANSMITTANCELUT], "skyAtmosphere_transmittanceLutCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT], "skyAtmosphere_multiScatteredLuminanceLutCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SKYATMOSPHERE_SKYVIEWLUT], "skyAtmosphere_skyViewLutCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SKYATMOSPHERE_SKYLUMINANCELUT], "skyAtmosphere_skyLuminanceLutCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SKYATMOSPHERE_CAMERAVOLUMELUT], "skyAtmosphere_cameraVolumeLutCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAIN2D_UNORM4], "generateMIPChain2DCS_unorm4.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAIN2D_FLOAT4], "generateMIPChain2DCS_float4.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAIN3D_UNORM4], "generateMIPChain3DCS_unorm4.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAIN3D_FLOAT4], "generateMIPChain3DCS_float4.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAINCUBE_UNORM4], "generateMIPChainCubeCS_unorm4.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAINCUBE_FLOAT4], "generateMIPChainCubeCS_float4.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAINCUBEARRAY_UNORM4], "generateMIPChainCubeArrayCS_unorm4.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAINCUBEARRAY_FLOAT4], "generateMIPChainCubeArrayCS_float4.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC1], "blockcompressCS_BC1.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC3], "blockcompressCS_BC3.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC4], "blockcompressCS_BC4.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC5], "blockcompressCS_BC5.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC6H], "blockcompressCS_BC6H.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC6H_CUBEMAP], "blockcompressCS_BC6H_cubemap.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_FILTERENVMAP], "filterEnvMapCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_COPYTEXTURE2D_UNORM4], "copytexture2D_unorm4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_COPYTEXTURE2D_FLOAT4], "copytexture2D_float4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_COPYTEXTURE2D_UNORM4_BORDEREXPAND], "copytexture2D_unorm4_borderexpandCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_COPYTEXTURE2D_FLOAT4_BORDEREXPAND], "copytexture2D_float4_borderexpandCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SKINNING], "skinningCS.cso"); });

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_PAINT_TEXTURE], "paint_textureCS.cso"); });

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT1], "blur_gaussian_float1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT3], "blur_gaussian_float3CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT4], "blur_gaussian_float4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_UNORM1], "blur_gaussian_unorm1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_UNORM4], "blur_gaussian_unorm4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT1], "blur_gaussian_wide_float1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT3], "blur_gaussian_wide_float3CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT4], "blur_gaussian_wide_float4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_UNORM1], "blur_gaussian_wide_unorm1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_UNORM4], "blur_gaussian_wide_unorm4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_FLOAT1], "blur_bilateral_float1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_FLOAT3], "blur_bilateral_float3CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_FLOAT4], "blur_bilateral_float4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_UNORM1], "blur_bilateral_unorm1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_UNORM4], "blur_bilateral_unorm4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_WIDE_FLOAT1], "blur_bilateral_wide_float1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_WIDE_FLOAT3], "blur_bilateral_wide_float3CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_WIDE_FLOAT4], "blur_bilateral_wide_float4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_WIDE_UNORM1], "blur_bilateral_wide_unorm1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_BILATERAL_WIDE_UNORM4], "blur_bilateral_wide_unorm4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSAO], "ssaoCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_HBAO], "hbaoCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MSAO_PREPAREDEPTHBUFFERS1], "msao_preparedepthbuffers1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MSAO_PREPAREDEPTHBUFFERS2], "msao_preparedepthbuffers2CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MSAO_INTERLEAVE], "msao_interleaveCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MSAO], "msaoCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MSAO_BLURUPSAMPLE], "msao_blurupsampleCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MSAO_BLURUPSAMPLE_BLENDOUT], "msao_blurupsampleCS_blendout.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MSAO_BLURUPSAMPLE_PREMIN], "msao_blurupsampleCS_premin.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MSAO_BLURUPSAMPLE_PREMIN_BLENDOUT], "msao_blurupsampleCS_premin_blendout.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSR_TILEMAXROUGHNESS_HORIZONTAL], "ssr_tileMaxRoughness_horizontalCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSR_TILEMAXROUGHNESS_VERTICAL], "ssr_tileMaxRoughness_verticalCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSR_DEPTHHIERARCHY], "ssr_depthHierarchyCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSR_RAYTRACE], "ssr_raytraceCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSR_RAYTRACE_EARLYEXIT], "ssr_raytraceCS_earlyexit.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSR_RAYTRACE_CHEAP], "ssr_raytraceCS_cheap.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSR_RESOLVE], "ssr_resolveCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSR_TEMPORAL], "ssr_temporalCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSR_UPSAMPLE], "ssr_upsampleCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_LIGHTSHAFTS], "lightShaftsCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_TILEMAXCOC_HORIZONTAL], "depthoffield_tileMaxCOC_horizontalCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_TILEMAXCOC_VERTICAL], "depthoffield_tileMaxCOC_verticalCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_NEIGHBORHOODMAXCOC], "depthoffield_neighborhoodMaxCOCCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_PREPASS], "depthoffield_prepassCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_PREPASS_EARLYEXIT], "depthoffield_prepassCS_earlyexit.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_MAIN], "depthoffield_mainCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_MAIN_EARLYEXIT], "depthoffield_mainCS_earlyexit.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_MAIN_CHEAP], "depthoffield_mainCS_cheap.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_POSTFILTER], "depthoffield_postfilterCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DEPTHOFFIELD_UPSAMPLE], "depthoffield_upsampleCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MOTIONBLUR_TILEMAXVELOCITY_HORIZONTAL], "motionblur_tileMaxVelocity_horizontalCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MOTIONBLUR_TILEMAXVELOCITY_VERTICAL], "motionblur_tileMaxVelocity_verticalCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MOTIONBLUR_NEIGHBORHOODMAXVELOCITY], "motionblur_neighborhoodMaxVelocityCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MOTIONBLUR], "motionblurCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MOTIONBLUR_EARLYEXIT], "motionblurCS_earlyexit.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_MOTIONBLUR_CHEAP], "motionblurCS_cheap.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLOOMSEPARATE], "bloomseparateCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_AERIALPERSPECTIVE], "aerialPerspectiveCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_AERIALPERSPECTIVE_CAPTURE], "aerialPerspectiveCS_capture.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_AERIALPERSPECTIVE_CAPTURE_MSAA], "aerialPerspectiveCS_capture_MSAA.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_SHAPENOISE], "volumetricCloud_shapenoiseCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_DETAILNOISE], "volumetricCloud_detailnoiseCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_CURLNOISE], "volumetricCloud_curlnoiseCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_WEATHERMAP], "volumetricCloud_weathermapCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_RENDER], "volumetricCloud_renderCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_RENDER_CAPTURE], "volumetricCloud_renderCS_capture.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_RENDER_CAPTURE_MSAA], "volumetricCloud_renderCS_capture_MSAA.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_REPROJECT], "volumetricCloud_reprojectCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_SHADOW_RENDER], "volumetricCloud_shadow_renderCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_SHADOW_FILTER], "volumetricCloud_shadow_filterCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FXAA], "fxaaCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_TEMPORALAA], "temporalaaCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SHARPEN], "sharpenCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_TONEMAP], "tonemapCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_UNDERWATER], "underwaterCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR_UPSCALING], "fsr_upscalingCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR_SHARPEN], "fsr_sharpenCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR2_AUTOGEN_REACTIVE_PASS], "ffx-fsr2/ffx_fsr2_autogen_reactive_pass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR2_COMPUTE_LUMINANCE_PYRAMID_PASS], "ffx-fsr2/ffx_fsr2_compute_luminance_pyramid_pass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR2_PREPARE_INPUT_COLOR_PASS], "ffx-fsr2/ffx_fsr2_prepare_input_color_pass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR2_RECONSTRUCT_PREVIOUS_DEPTH_PASS], "ffx-fsr2/ffx_fsr2_reconstruct_previous_depth_pass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR2_DEPTH_CLIP_PASS], "ffx-fsr2/ffx_fsr2_depth_clip_pass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR2_LOCK_PASS], "ffx-fsr2/ffx_fsr2_lock_pass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR2_ACCUMULATE_PASS], "ffx-fsr2/ffx_fsr2_accumulate_pass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_FSR2_RCAS_PASS], "ffx-fsr2/ffx_fsr2_rcas_pass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_CHROMATIC_ABERRATION], "chromatic_aberrationCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_UPSAMPLE_BILATERAL_FLOAT1], "upsample_bilateral_float1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_UPSAMPLE_BILATERAL_UNORM1], "upsample_bilateral_unorm1CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_UPSAMPLE_BILATERAL_FLOAT4], "upsample_bilateral_float4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_UPSAMPLE_BILATERAL_UNORM4], "upsample_bilateral_unorm4CS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_DOWNSAMPLE4X], "downsample4xCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_LINEARDEPTH], "lineardepthCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_NORMALSFROMDEPTH], "normalsfromdepthCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SCREENSPACESHADOW], "screenspaceshadowCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSGI_DEINTERLEAVE], "ssgi_deinterleaveCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSGI], "ssgiCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSGI_WIDE], "ssgiCS.cso", wi::graphics::ShaderModel::SM_5_0, { "WIDE" }); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSGI_UPSAMPLE], "ssgi_upsampleCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_SSGI_UPSAMPLE_WIDE], "ssgi_upsampleCS.cso", wi::graphics::ShaderModel::SM_5_0, { "WIDE" }); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTDIFFUSE_SPATIAL], "rtdiffuse_spatialCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTDIFFUSE_TEMPORAL], "rtdiffuse_temporalCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTDIFFUSE_UPSAMPLE], "rtdiffuse_upsampleCS.cso"); });

		if (device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
		{
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTDIFFUSE], "rtdiffuseCS.cso", ShaderModel::SM_6_5); });

			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTREFLECTION], "rtreflectionCS.cso", ShaderModel::SM_6_5); });

			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTSHADOW], "rtshadowCS.cso", ShaderModel::SM_6_5); });
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTSHADOW_DENOISE_TILECLASSIFICATION], "rtshadow_denoise_tileclassificationCS.cso"); });
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTSHADOW_DENOISE_FILTER], "rtshadow_denoise_filterCS.cso"); });
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTSHADOW_DENOISE_TEMPORAL], "rtshadow_denoise_temporalCS.cso"); });

			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTAO], "rtaoCS.cso", ShaderModel::SM_6_5); });
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTAO_DENOISE_TILECLASSIFICATION], "rtao_denoise_tileclassificationCS.cso"); });
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTAO_DENOISE_FILTER], "rtao_denoise_filterCS.cso"); });

		}

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_RTSHADOW_UPSAMPLE], "rtshadow_upsampleCS.cso"); });

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SURFEL_COVERAGE], "surfel_coverageCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SURFEL_INDIRECTPREPARE], "surfel_indirectprepareCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SURFEL_UPDATE], "surfel_updateCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SURFEL_GRIDOFFSETS], "surfel_gridoffsetsCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SURFEL_BINNING], "surfel_binningCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SURFEL_INTEGRATE], "surfel_integrateCS.cso"); });
		if (device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
		{
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SURFEL_RAYTRACE], "surfel_raytraceCS_rtapi.cso", ShaderModel::SM_6_5); });
		}
		else
		{
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SURFEL_RAYTRACE], "surfel_raytraceCS.cso"); });
		}

		if (device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
		{
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_RAYTRACE], "raytraceCS_rtapi.cso", ShaderModel::SM_6_5); });
		}
		else
		{
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_RAYTRACE], "raytraceCS.cso"); });
		}

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VISIBILITY_RESOLVE], "visibility_resolveCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VISIBILITY_RESOLVE_MSAA], "visibility_resolveCS_MSAA.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VISIBILITY_SKY], "visibility_skyCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VISIBILITY_VELOCITY], "visibility_velocityCS.cso"); });

		if (device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
		{
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_DDGI_RAYTRACE], "ddgi_raytraceCS_rtapi.cso", ShaderModel::SM_6_5); });
		}
		else
		{
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_DDGI_RAYTRACE], "ddgi_raytraceCS.cso"); });
		}
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_DDGI_RAYALLOCATION], "ddgi_rayallocationCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_DDGI_INDIRECTPREPARE], "ddgi_indirectprepareCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_DDGI_UPDATE], "ddgi_updateCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_DDGI_UPDATE_DEPTH], "ddgi_updateCS_depth.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_TERRAIN_VIRTUALTEXTURE_UPDATE_BASECOLORMAP], "terrainVirtualTextureUpdateCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_TERRAIN_VIRTUALTEXTURE_UPDATE_NORMALMAP], "terrainVirtualTextureUpdateCS_normalmap.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_TERRAIN_VIRTUALTEXTURE_UPDATE_SURFACEMAP], "terrainVirtualTextureUpdateCS_surfacemap.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_TERRAIN_VIRTUALTEXTURE_UPDATE_EMISSIVEMAP], "terrainVirtualTextureUpdateCS_emissivemap.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_MESHLET_PREPARE], "meshlet_prepareCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_IMPOSTOR_PREPARE], "impostor_prepareCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VIRTUALTEXTURE_TILEREQUESTS], "virtualTextureTileRequestsCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VIRTUALTEXTURE_TILEALLOCATE], "virtualTextureTileAllocateCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VIRTUALTEXTURE_RESIDENCYUPDATE], "virtualTextureResidencyUpdateCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_WIND], "windCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_YUV_TO_RGB], "yuv_to_rgbCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_WETMAP_UPDATE], "wetmap_updateCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_CAUSTICS], "causticsCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_DEPTH_REPROJECT], "depth_reprojectCS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_DEPTH_PYRAMID], "depth_pyramidCS.cso"); });

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::HS, shaders[HSTYPE_OBJECT], "objectHS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::HS, shaders[HSTYPE_OBJECT_PREPASS], "objectHS_prepass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::HS, shaders[HSTYPE_OBJECT_PREPASS_ALPHATEST], "objectHS_prepass_alphatest.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::HS, shaders[HSTYPE_OBJECT_SIMPLE], "objectHS_simple.cso"); });

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::DS, shaders[DSTYPE_OBJECT], "objectDS.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::DS, shaders[DSTYPE_OBJECT_PREPASS], "objectDS_prepass.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::DS, shaders[DSTYPE_OBJECT_PREPASS_ALPHATEST], "objectDS_prepass_alphatest.cso"); });
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::DS, shaders[DSTYPE_OBJECT_SIMPLE], "objectDS_simple.cso"); });

		wi::jobsystem::Dispatch(objectps_ctx, MaterialComponent::SHADERTYPE_COUNT, 1, [](wi::jobsystem::JobArgs args) {

			LoadShader(
				ShaderStage::PS,
				shaders[PSTYPE_OBJECT_PERMUTATION_BEGIN + args.jobIndex],
				"objectPS.cso",
				ShaderModel::SM_6_0,
				MaterialComponent::shaderTypeDefines[args.jobIndex] // permutation defines
			);

			});

		wi::jobsystem::Dispatch(objectps_ctx, MaterialComponent::SHADERTYPE_COUNT, 1, [](wi::jobsystem::JobArgs args) {

			auto defines = MaterialComponent::shaderTypeDefines[args.jobIndex];
			defines.push_back("TRANSPARENT");
			LoadShader(
				ShaderStage::PS,
				shaders[PSTYPE_OBJECT_TRANSPARENT_PERMUTATION_BEGIN + args.jobIndex],
				"objectPS.cso",
				ShaderModel::SM_6_0,
				defines // permutation defines
			);

			});

		wi::jobsystem::Wait(ctx);

		if (device->CheckCapability(GraphicsDeviceCapability::MESH_SHADER))
		{
			// Note: Mesh shader loading is very slow in Vulkan, so all mesh shader loading will be executed on a separate context
			//	and only waited by mesh shader PSO jobs, not holding back the rest of initialization
			wi::jobsystem::Wait(mesh_shader_ctx);
			mesh_shader_ctx.priority = wi::jobsystem::Priority::Low;

			wi::jobsystem::Execute(mesh_shader_ctx, [](wi::jobsystem::JobArgs args) {
				LoadShader(ShaderStage::AS, shaders[ASTYPE_OBJECT], "objectAS.cso");
				LoadShader(ShaderStage::MS, shaders[MSTYPE_OBJECT_SIMPLE], "objectMS_simple.cso");

				PipelineStateDesc desc;
				desc.as = &shaders[ASTYPE_OBJECT];
				desc.ms = &shaders[MSTYPE_OBJECT_SIMPLE];
				desc.ps = &shaders[PSTYPE_OBJECT_SIMPLE]; // this is created in a different thread, so wait for the ctx before getting here
				desc.rs = &rasterizers[RSTYPE_WIRE];
				desc.bs = &blendStates[BSTYPE_OPAQUE];
				desc.dss = &depthStencils[DSSTYPE_DEFAULT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				device->CreatePipelineState(&desc, &PSO_object_wire_mesh_shader);
				});

			wi::jobsystem::Execute(mesh_shader_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::MS, shaders[MSTYPE_OBJECT], "objectMS.cso"); });
			wi::jobsystem::Execute(mesh_shader_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::MS, shaders[MSTYPE_OBJECT_PREPASS], "objectMS_prepass.cso"); });
			wi::jobsystem::Execute(mesh_shader_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::MS, shaders[MSTYPE_OBJECT_PREPASS_ALPHATEST], "objectMS_prepass_alphatest.cso"); });

			wi::jobsystem::Execute(mesh_shader_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::MS, shaders[MSTYPE_SHADOW], "shadowMS.cso"); });
			wi::jobsystem::Execute(mesh_shader_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::MS, shaders[MSTYPE_SHADOW_ALPHATEST], "shadowMS_alphatest.cso"); });
			wi::jobsystem::Execute(mesh_shader_ctx, [](wi::jobsystem::JobArgs args) { LoadShader(ShaderStage::MS, shaders[MSTYPE_SHADOW_TRANSPARENT], "shadowMS_transparent.cso"); });
		}

		wi::jobsystem::Dispatch(ctx, MaterialComponent::SHADERTYPE_COUNT, 1, [](wi::jobsystem::JobArgs args) {

			LoadShader(
				ShaderStage::CS,
				shaders[CSTYPE_VISIBILITY_SURFACE_PERMUTATION_BEGIN + args.jobIndex],
				"visibility_surfaceCS.cso",
				ShaderModel::SM_6_0,
				MaterialComponent::shaderTypeDefines[args.jobIndex] // permutation defines
			);

			});

		wi::jobsystem::Dispatch(ctx, MaterialComponent::SHADERTYPE_COUNT, 1, [](wi::jobsystem::JobArgs args) {

			auto defines = MaterialComponent::shaderTypeDefines[args.jobIndex];
			defines.push_back("REDUCED");
			LoadShader(
				ShaderStage::CS,
				shaders[CSTYPE_VISIBILITY_SURFACE_REDUCED_PERMUTATION_BEGIN + args.jobIndex],
				"visibility_surfaceCS.cso",
				ShaderModel::SM_6_0,
				defines // permutation defines
			);

			});

		wi::jobsystem::Dispatch(ctx, MaterialComponent::SHADERTYPE_COUNT, 1, [](wi::jobsystem::JobArgs args) {

			LoadShader(
				ShaderStage::CS,
				shaders[CSTYPE_VISIBILITY_SHADE_PERMUTATION_BEGIN + args.jobIndex],
				"visibility_shadeCS.cso",
				ShaderModel::SM_6_0,
				MaterialComponent::shaderTypeDefines[args.jobIndex] // permutation defines
			);

			});

		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_OBJECT_SIMPLE];
			desc.ps = &shaders[PSTYPE_OBJECT_SIMPLE];
			desc.rs = &rasterizers[RSTYPE_WIRE];
			desc.bs = &blendStates[BSTYPE_OPAQUE];
			desc.dss = &depthStencils[DSSTYPE_DEFAULT];

			device->CreatePipelineState(&desc, &PSO_object_wire);

			desc.pt = PrimitiveTopology::PATCHLIST;
			desc.vs = &shaders[VSTYPE_OBJECT_SIMPLE_TESSELLATION];
			desc.hs = &shaders[HSTYPE_OBJECT_SIMPLE];
			desc.ds = &shaders[DSTYPE_OBJECT_SIMPLE];
			device->CreatePipelineState(&desc, &PSO_object_wire_tessellation);
			});
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_OCCLUDEE];
			desc.rs = &rasterizers[RSTYPE_OCCLUDEE];
			desc.bs = &blendStates[BSTYPE_COLORWRITEDISABLE];
			desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
			desc.pt = PrimitiveTopology::TRIANGLESTRIP;

			device->CreatePipelineState(&desc, &PSO_occlusionquery);
			});
		wi::jobsystem::Dispatch(ctx, RENDERPASS_COUNT, 1, [](wi::jobsystem::JobArgs args) {
			const bool impostorRequest =
				args.jobIndex != RENDERPASS_VOXELIZE &&
				args.jobIndex != RENDERPASS_SHADOW &&
				args.jobIndex != RENDERPASS_ENVMAPCAPTURE;
			if (!impostorRequest)
			{
				return;
			}

			PipelineStateDesc desc;
			desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
			desc.bs = &blendStates[BSTYPE_OPAQUE];
			desc.dss = &depthStencils[DSSTYPE_DEFAULT];
			desc.il = nullptr;

			switch (args.jobIndex)
			{
			case RENDERPASS_MAIN:
				desc.dss = &depthStencils[DSSTYPE_DEPTHREADEQUAL];
				desc.vs = &shaders[VSTYPE_IMPOSTOR];
				desc.ps = &shaders[PSTYPE_IMPOSTOR];
				break;
			case RENDERPASS_PREPASS:
				desc.vs = &shaders[VSTYPE_IMPOSTOR];
				desc.ps = &shaders[PSTYPE_IMPOSTOR_PREPASS];
				break;
			case RENDERPASS_PREPASS_DEPTHONLY:
				desc.vs = &shaders[VSTYPE_IMPOSTOR];
				desc.ps = &shaders[PSTYPE_IMPOSTOR_PREPASS_DEPTHONLY];
				break;
			default:
				desc.vs = &shaders[VSTYPE_IMPOSTOR];
				desc.ps = &shaders[PSTYPE_IMPOSTOR_PREPASS];
				break;
			}

			device->CreatePipelineState(&desc, &PSO_impostor[args.jobIndex]);
			});
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_IMPOSTOR];
			desc.ps = &shaders[PSTYPE_IMPOSTOR_SIMPLE];
			desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED];
			desc.bs = &blendStates[BSTYPE_OPAQUE];
			desc.dss = &depthStencils[DSSTYPE_DEFAULT];
			desc.il = nullptr;

			device->CreatePipelineState(&desc, &PSO_impostor_wire);
			});
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_OBJECT_COMMON];
			desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
			desc.bs = &blendStates[BSTYPE_OPAQUE];
			desc.dss = &depthStencils[DSSTYPE_CAPTUREIMPOSTOR];

			desc.ps = &shaders[PSTYPE_CAPTUREIMPOSTOR];
			device->CreatePipelineState(&desc, &PSO_captureimpostor);
			});

		wi::jobsystem::Dispatch(ctx, LightComponent::LIGHTTYPE_COUNT, 1, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;

			// deferred lights:

			desc.pt = PrimitiveTopology::TRIANGLELIST;


			// light visualizers:
			if (args.jobIndex != LightComponent::DIRECTIONAL)
			{

				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.ps = &shaders[PSTYPE_LIGHTVISUALIZER];

				switch (args.jobIndex)
				{
				case LightComponent::POINT:
					desc.bs = &blendStates[BSTYPE_ADDITIVE];
					desc.vs = &shaders[VSTYPE_LIGHTVISUALIZER_POINTLIGHT];
					desc.rs = &rasterizers[RSTYPE_FRONT];
					desc.il = &inputLayouts[ILTYPE_POSITION];
					break;
				case LightComponent::SPOT:
					desc.bs = &blendStates[BSTYPE_ADDITIVE];
					desc.vs = &shaders[VSTYPE_LIGHTVISUALIZER_SPOTLIGHT];
					desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
					break;
				}

				device->CreatePipelineState(&desc, &PSO_lightvisualizer[args.jobIndex]);
			}


			// volumetric lights:
			if (args.jobIndex <= LightComponent::SPOT)
			{
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.bs = &blendStates[BSTYPE_ADDITIVE];
				desc.rs = &rasterizers[RSTYPE_BACK];

				switch (args.jobIndex)
				{
				case LightComponent::DIRECTIONAL:
					desc.vs = &shaders[VSTYPE_VOLUMETRICLIGHT_DIRECTIONAL];
					desc.ps = &shaders[PSTYPE_VOLUMETRICLIGHT_DIRECTIONAL];
					break;
				case LightComponent::POINT:
					desc.vs = &shaders[VSTYPE_VOLUMETRICLIGHT_POINT];
					desc.ps = &shaders[PSTYPE_VOLUMETRICLIGHT_POINT];
					break;
				case LightComponent::SPOT:
					desc.vs = &shaders[VSTYPE_VOLUMETRICLIGHT_SPOT];
					desc.ps = &shaders[PSTYPE_VOLUMETRICLIGHT_SPOT];
					break;
				}

				device->CreatePipelineState(&desc, &PSO_volumetriclight[args.jobIndex]);
			}


			});
		wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, shaders[VSTYPE_RENDERLIGHTMAP], "renderlightmapVS.cso");
			if (device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
			{
				LoadShader(ShaderStage::PS, shaders[PSTYPE_RENDERLIGHTMAP], "renderlightmapPS_rtapi.cso", ShaderModel::SM_6_5);
			}
			else
			{
				LoadShader(ShaderStage::PS, shaders[PSTYPE_RENDERLIGHTMAP], "renderlightmapPS.cso");
			}
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_RENDERLIGHTMAP];
			desc.ps = &shaders[PSTYPE_RENDERLIGHTMAP];
			desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
			desc.bs = &blendStates[BSTYPE_TRANSPARENT];
			desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];

			RenderPassInfo renderpass_info;
			renderpass_info.rt_count = 1;
			renderpass_info.rt_formats[0] = Format::R32G32B32A32_FLOAT;

			device->CreatePipelineState(&desc, &PSO_renderlightmap, &renderpass_info);
			});
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_POSTPROCESS];
			desc.ps = &shaders[PSTYPE_DOWNSAMPLEDEPTHBUFFER];
			desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
			desc.bs = &blendStates[BSTYPE_OPAQUE];
			desc.dss = &depthStencils[DSSTYPE_WRITEONLY];

			device->CreatePipelineState(&desc, &PSO_downsampledepthbuffer);
			});
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_POSTPROCESS];
			desc.ps = &shaders[PSTYPE_POSTPROCESS_UPSAMPLE_BILATERAL];
			desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
			desc.bs = &blendStates[BSTYPE_PREMULTIPLIED];
			desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];

			device->CreatePipelineState(&desc, &PSO_upsample_bilateral);
			});
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_POSTPROCESS];
			desc.ps = &shaders[PSTYPE_POSTPROCESS_VOLUMETRICCLOUDS_UPSAMPLE];
			desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
			desc.bs = &blendStates[BSTYPE_PREMULTIPLIED];
			desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];

			device->CreatePipelineState(&desc, &PSO_volumetricclouds_upsample);
			});
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_POSTPROCESS];
			desc.ps = &shaders[PSTYPE_POSTPROCESS_OUTLINE];
			desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
			desc.bs = &blendStates[BSTYPE_TRANSPARENT];
			desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];

			device->CreatePipelineState(&desc, &PSO_outline);
			});
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;

			desc.vs = &shaders[VSTYPE_SCREEN];
			desc.ps = &shaders[PSTYPE_COPY_DEPTH];
			desc.bs = &blendStates[BSTYPE_OPAQUE];
			desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
			desc.dss = &depthStencils[DSSTYPE_WRITEONLY];
			device->CreatePipelineState(&desc, &PSO_copyDepth);

			desc.ps = &shaders[PSTYPE_COPY_STENCIL_BIT];
			for (int i = 0; i < 8; ++i)
			{
				desc.dss = &depthStencils[DSSTYPE_COPY_STENCIL_BIT_0 + i];
				device->CreatePipelineState(&desc, &PSO_copyStencilBit[i]);
			}
			});
		wi::jobsystem::Execute(ctx, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_LENSFLARE];
			desc.ps = &shaders[PSTYPE_LENSFLARE];
			desc.bs = &blendStates[BSTYPE_ADDITIVE];
			desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
			desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
			desc.pt = PrimitiveTopology::TRIANGLESTRIP;

			device->CreatePipelineState(&desc, &PSO_lensflare);
			});
		wi::jobsystem::Dispatch(ctx, SKYRENDERING_COUNT, 1, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.rs = &rasterizers[RSTYPE_SKY];
			desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];

			switch (args.jobIndex)
			{
			case SKYRENDERING_STATIC:
				desc.bs = &blendStates[BSTYPE_OPAQUE];
				desc.vs = &shaders[VSTYPE_SKY];
				desc.ps = &shaders[PSTYPE_SKY_STATIC];
				break;
			case SKYRENDERING_DYNAMIC:
				desc.bs = &blendStates[BSTYPE_OPAQUE];
				desc.vs = &shaders[VSTYPE_SKY];
				desc.ps = &shaders[PSTYPE_SKY_DYNAMIC];
				break;
			case SKYRENDERING_SUN:
				desc.bs = &blendStates[BSTYPE_ADDITIVE];
				desc.vs = &shaders[VSTYPE_SKY];
				desc.ps = &shaders[PSTYPE_SUN];
				break;
			case SKYRENDERING_ENVMAPCAPTURE_STATIC:
				desc.bs = &blendStates[BSTYPE_OPAQUE];
				desc.vs = &shaders[VSTYPE_ENVMAP_SKY];
				desc.ps = &shaders[PSTYPE_ENVMAP_SKY_STATIC];
				if (!device->CheckCapability(GraphicsDeviceCapability::RENDERTARGET_AND_VIEWPORT_ARRAYINDEX_WITHOUT_GS))
				{
					desc.gs = &shaders[GSTYPE_ENVMAP_SKY_EMULATION];
				}
				break;
			case SKYRENDERING_ENVMAPCAPTURE_DYNAMIC:
				desc.bs = &blendStates[BSTYPE_OPAQUE];
				desc.vs = &shaders[VSTYPE_ENVMAP_SKY];
				desc.ps = &shaders[PSTYPE_ENVMAP_SKY_DYNAMIC];
				if (!device->CheckCapability(GraphicsDeviceCapability::RENDERTARGET_AND_VIEWPORT_ARRAYINDEX_WITHOUT_GS))
				{
					desc.gs = &shaders[GSTYPE_ENVMAP_SKY_EMULATION];
				}
				break;
			}

			device->CreatePipelineState(&desc, &PSO_sky[args.jobIndex]);
			});
		wi::jobsystem::Dispatch(ctx, DEBUGRENDERING_COUNT, 1, [](wi::jobsystem::JobArgs args) {
			PipelineStateDesc desc;

			switch (args.jobIndex)
			{
			case DEBUGRENDERING_ENVPROBE:
				desc.vs = &shaders[VSTYPE_SPHERE];
				desc.ps = &shaders[PSTYPE_CUBEMAP];
				desc.dss = &depthStencils[DSSTYPE_DEFAULT];
				desc.rs = &rasterizers[RSTYPE_FRONT];
				desc.bs = &blendStates[BSTYPE_OPAQUE];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_DDGI:
				desc.vs = &shaders[VSTYPE_DDGI_DEBUG];
				desc.ps = &shaders[PSTYPE_DDGI_DEBUG];
				desc.dss = &depthStencils[DSSTYPE_DEFAULT];
				desc.rs = &rasterizers[RSTYPE_FRONT];
				desc.bs = &blendStates[BSTYPE_OPAQUE];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_GRID:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_CUBE:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_CUBE_DEPTH:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_LINES:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_LINES_DEPTH:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_SOLID:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_WIREFRAME:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_SOLID_DEPTH:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_WIREFRAME_DEPTH:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_EMITTER:
				desc.vs = &shaders[VSTYPE_OBJECT_DEBUG];
				desc.ps = &shaders[PSTYPE_OBJECT_DEBUG];
				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_OPAQUE];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_PAINTRADIUS:
				desc.vs = &shaders[VSTYPE_OBJECT_SIMPLE];
				desc.ps = &shaders[PSTYPE_OBJECT_PAINTRADIUS];
				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rasterizers[RSTYPE_FRONT];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_VOXEL:
				desc.vs = &shaders[VSTYPE_VOXEL];
				desc.ps = &shaders[PSTYPE_VOXEL];
				desc.gs = &shaders[GSTYPE_VOXEL];
				desc.dss = &depthStencils[DSSTYPE_DEFAULT];
				desc.rs = &rasterizers[RSTYPE_BACK];
				desc.bs = &blendStates[BSTYPE_OPAQUE];
				desc.pt = PrimitiveTopology::POINTLIST;
				break;
			case DEBUGRENDERING_FORCEFIELD_POINT:
				desc.vs = &shaders[VSTYPE_FORCEFIELDVISUALIZER_POINT];
				desc.ps = &shaders[PSTYPE_FORCEFIELDVISUALIZER];
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rasterizers[RSTYPE_BACK];
				desc.bs = &blendStates[BSTYPE_ADDITIVE];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_FORCEFIELD_PLANE:
				desc.vs = &shaders[VSTYPE_FORCEFIELDVISUALIZER_PLANE];
				desc.ps = &shaders[PSTYPE_FORCEFIELDVISUALIZER];
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rasterizers[RSTYPE_FRONT];
				desc.bs = &blendStates[BSTYPE_ADDITIVE];
				desc.pt = PrimitiveTopology::TRIANGLESTRIP;
				break;
			case DEBUGRENDERING_RAYTRACE_BVH:
				desc.vs = &shaders[VSTYPE_RAYTRACE_SCREEN];
				desc.ps = &shaders[PSTYPE_RAYTRACE_DEBUGBVH];
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			}

			device->CreatePipelineState(&desc, &PSO_debug[args.jobIndex]);
			});

#ifdef RTREFLECTION_WITH_RAYTRACING_PIPELINE
		if (device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
		{
			wi::jobsystem::Execute(raytracing_ctx, [](wi::jobsystem::JobArgs args) {

				bool success = LoadShader(ShaderStage::LIB, shaders[RTTYPE_RTREFLECTION], "rtreflectionLIB.cso");
				assert(success);

				RaytracingPipelineStateDesc rtdesc;
				rtdesc.shader_libraries.emplace_back();
				rtdesc.shader_libraries.back().shader = &shaders[RTTYPE_RTREFLECTION];
				rtdesc.shader_libraries.back().function_name = "RTReflection_Raygen";
				rtdesc.shader_libraries.back().type = ShaderLibrary::Type::RAYGENERATION;

				rtdesc.shader_libraries.emplace_back();
				rtdesc.shader_libraries.back().shader = &shaders[RTTYPE_RTREFLECTION];
				rtdesc.shader_libraries.back().function_name = "RTReflection_ClosestHit";
				rtdesc.shader_libraries.back().type = ShaderLibrary::Type::CLOSESTHIT;

				rtdesc.shader_libraries.emplace_back();
				rtdesc.shader_libraries.back().shader = &shaders[RTTYPE_RTREFLECTION];
				rtdesc.shader_libraries.back().function_name = "RTReflection_AnyHit";
				rtdesc.shader_libraries.back().type = ShaderLibrary::Type::ANYHIT;

				rtdesc.shader_libraries.emplace_back();
				rtdesc.shader_libraries.back().shader = &shaders[RTTYPE_RTREFLECTION];
				rtdesc.shader_libraries.back().function_name = "RTReflection_Miss";
				rtdesc.shader_libraries.back().type = ShaderLibrary::Type::MISS;

				rtdesc.hit_groups.emplace_back();
				rtdesc.hit_groups.back().type = ShaderHitGroup::Type::GENERAL;
				rtdesc.hit_groups.back().name = "RTReflection_Raygen";
				rtdesc.hit_groups.back().general_shader = 0;

				rtdesc.hit_groups.emplace_back();
				rtdesc.hit_groups.back().type = ShaderHitGroup::Type::GENERAL;
				rtdesc.hit_groups.back().name = "RTReflection_Miss";
				rtdesc.hit_groups.back().general_shader = 3;

				rtdesc.hit_groups.emplace_back();
				rtdesc.hit_groups.back().type = ShaderHitGroup::Type::TRIANGLES;
				rtdesc.hit_groups.back().name = "RTReflection_Hitgroup";
				rtdesc.hit_groups.back().closest_hit_shader = 1;
				rtdesc.hit_groups.back().any_hit_shader = 2;

				rtdesc.max_trace_recursion_depth = 1;
				rtdesc.max_payload_size_in_bytes = sizeof(float4);
				rtdesc.max_attribute_size_in_bytes = sizeof(float2); // bary
				success = device->CreateRaytracingPipelineState(&rtdesc, &RTPSO_reflection);


				});
		};
#endif // RTREFLECTION_WITH_RAYTRACING_PIPELINE

		// Clear custom shaders (Custom shaders coming from user will need to be handled by the user in case of shader reload):
		customShaders.clear();

		// Hologram sample shader will be registered as custom shader:
		//	It's best to register all custom shaders from the same thread, so under here
		//	or after engine has been completely initialized
		//	This is because RegisterCustomShader() will give out IDs in increasing order
		//	and you can keep the order stable by ensuring they are registered in order.
		{
			SHADERTYPE realVS = GetVSTYPE(RENDERPASS_MAIN, false, false, true);

			PipelineStateDesc desc;
			desc.vs = &shaders[realVS];
			desc.ps = &shaders[PSTYPE_OBJECT_HOLOGRAM];

			desc.bs = &blendStates[BSTYPE_ADDITIVE];
			desc.rs = &rasterizers[RSTYPE_FRONT];
			desc.dss = &depthStencils[DSSTYPE_HOLOGRAM];
			desc.pt = PrimitiveTopology::TRIANGLELIST;

			PipelineState pso;
			device->CreatePipelineState(&desc, &pso);

			CustomShader customShader;
			customShader.name = "Hologram";
			customShader.filterMask = FILTER_TRANSPARENT;
			customShader.pso[RENDERPASS_MAIN] = pso;
			RegisterCustomShader(customShader);
		}


		wi::jobsystem::Wait(ctx);



		for (uint32_t renderPass = 0; renderPass < RENDERPASS_COUNT; ++renderPass)
		{
			for (uint32_t mesh_shader = 0; mesh_shader <= (device->CheckCapability(GraphicsDeviceCapability::MESH_SHADER) ? 1u : 0u); ++mesh_shader)
			{
				// default objectshaders:
				//	We don't wait for these here, because then it can slow down the init time a lot
				//	We will wait for these to complete in RenderMeshes() just before they will be first used
				wi::jobsystem::Wait(object_pso_job_ctx[renderPass][mesh_shader]);
				object_pso_job_ctx[renderPass][mesh_shader].priority = wi::jobsystem::Priority::Low;
				for (uint32_t shaderType = 0; shaderType < MaterialComponent::SHADERTYPE_COUNT; ++shaderType)
				{
					wi::jobsystem::Execute(object_pso_job_ctx[renderPass][mesh_shader], [=](wi::jobsystem::JobArgs args) {
						for (uint32_t blendMode = 0; blendMode < BLENDMODE_COUNT; ++blendMode)
						{
							for (uint32_t cullMode = 0; cullMode <= 3; ++cullMode)
							{
								for (uint32_t tessellation = 0; tessellation <= 1; ++tessellation)
								{
									if (tessellation && renderPass > RENDERPASS_PREPASS_DEPTHONLY)
										continue;
									for (uint32_t alphatest = 0; alphatest <= 1; ++alphatest)
									{
										const bool transparency = blendMode != BLENDMODE_OPAQUE;
										if ((renderPass == RENDERPASS_PREPASS || renderPass == RENDERPASS_PREPASS_DEPTHONLY) && transparency)
											continue;

										PipelineStateDesc desc;

										if (mesh_shader)
										{
											if (tessellation)
												continue;
											SHADERTYPE realAS = GetASTYPE((RENDERPASS)renderPass, tessellation, alphatest, transparency, mesh_shader);
											SHADERTYPE realMS = GetMSTYPE((RENDERPASS)renderPass, tessellation, alphatest, transparency, mesh_shader);
											if (realMS == SHADERTYPE_COUNT)
												continue;
											desc.as = realAS < SHADERTYPE_COUNT ? &shaders[realAS] : nullptr;
											desc.ms = realMS < SHADERTYPE_COUNT ? &shaders[realMS] : nullptr;
										}
										else
										{
											SHADERTYPE realVS = GetVSTYPE((RENDERPASS)renderPass, tessellation, alphatest, transparency);
											SHADERTYPE realHS = GetHSTYPE((RENDERPASS)renderPass, tessellation, alphatest);
											SHADERTYPE realDS = GetDSTYPE((RENDERPASS)renderPass, tessellation, alphatest);
											SHADERTYPE realGS = GetGSTYPE((RENDERPASS)renderPass, alphatest, transparency);

											if (tessellation && (realHS == SHADERTYPE_COUNT || realDS == SHADERTYPE_COUNT))
												continue;

											desc.vs = realVS < SHADERTYPE_COUNT ? &shaders[realVS] : nullptr;
											desc.hs = realHS < SHADERTYPE_COUNT ? &shaders[realHS] : nullptr;
											desc.ds = realDS < SHADERTYPE_COUNT ? &shaders[realDS] : nullptr;
											desc.gs = realGS < SHADERTYPE_COUNT ? &shaders[realGS] : nullptr;
										}

										SHADERTYPE realPS = GetPSTYPE((RENDERPASS)renderPass, alphatest, transparency, (MaterialComponent::SHADERTYPE)shaderType);
										desc.ps = realPS < SHADERTYPE_COUNT ? &shaders[realPS] : nullptr;

										switch (blendMode)
										{
										case BLENDMODE_OPAQUE:
											desc.bs = &blendStates[BSTYPE_OPAQUE];
											break;
										case BLENDMODE_ALPHA:
											desc.bs = &blendStates[BSTYPE_TRANSPARENT];
											break;
										case BLENDMODE_ADDITIVE:
											desc.bs = &blendStates[BSTYPE_ADDITIVE];
											break;
										case BLENDMODE_PREMULTIPLIED:
											desc.bs = &blendStates[BSTYPE_PREMULTIPLIED];
											break;
										case BLENDMODE_MULTIPLY:
											desc.bs = &blendStates[BSTYPE_MULTIPLY];
											break;
										default:
											assert(0);
											break;
										}

										switch (renderPass)
										{
										case RENDERPASS_SHADOW:
											desc.bs = &blendStates[transparency ? BSTYPE_TRANSPARENTSHADOW : BSTYPE_COLORWRITEDISABLE];
											break;
										case RENDERPASS_RAINBLOCKER:
											desc.bs = &blendStates[BSTYPE_COLORWRITEDISABLE];
											break;
										default:
											break;
										}

										switch (renderPass)
										{
										case RENDERPASS_SHADOW:
											desc.dss = &depthStencils[transparency ? DSSTYPE_DEPTHREAD : DSSTYPE_SHADOW];
											break;
										case RENDERPASS_MAIN:
											if (blendMode == BLENDMODE_ADDITIVE)
											{
												desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
											}
											else
											{
												desc.dss = &depthStencils[transparency ? DSSTYPE_TRANSPARENT : DSSTYPE_DEPTHREADEQUAL];
											}
											break;
										case RENDERPASS_ENVMAPCAPTURE:
											desc.dss = &depthStencils[DSSTYPE_ENVMAP];
											break;
										case RENDERPASS_VOXELIZE:
											desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
											break;
										case RENDERPASS_RAINBLOCKER:
											desc.dss = &depthStencils[DSSTYPE_DEFAULT];
											break;
										default:
											if (blendMode == BLENDMODE_ADDITIVE)
											{
												desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
											}
											else
											{
												desc.dss = &depthStencils[DSSTYPE_DEFAULT];
											}
											break;
										}

										switch (renderPass)
										{
										case RENDERPASS_SHADOW:
											desc.rs = &rasterizers[cullMode == (int)CullMode::NONE ? RSTYPE_SHADOW_DOUBLESIDED : RSTYPE_SHADOW];
											break;
										case RENDERPASS_VOXELIZE:
											desc.rs = &rasterizers[RSTYPE_VOXELIZE];
											break;
										default:
											switch ((CullMode)cullMode)
											{
											default:
											case CullMode::BACK:
												desc.rs = &rasterizers[RSTYPE_FRONT];
												break;
											case CullMode::NONE:
												desc.rs = &rasterizers[RSTYPE_DOUBLESIDED];
												break;
											case CullMode::FRONT:
												desc.rs = &rasterizers[RSTYPE_BACK];
												break;
											}
											break;
										}

										if (tessellation)
										{
											desc.pt = PrimitiveTopology::PATCHLIST;
										}
										else
										{
											desc.pt = PrimitiveTopology::TRIANGLELIST;
										}

										wi::jobsystem::Wait(objectps_ctx);
										if (mesh_shader)
										{
											wi::jobsystem::Wait(mesh_shader_ctx);
										}

										ObjectRenderingVariant variant = {};
										variant.bits.renderpass = renderPass;
										variant.bits.shadertype = shaderType;
										variant.bits.blendmode = blendMode;
										variant.bits.cullmode = cullMode;
										variant.bits.tessellation = tessellation;
										variant.bits.alphatest = alphatest;
										variant.bits.sample_count = 1;
										variant.bits.mesh_shader = mesh_shader;

										switch (renderPass)
										{
										case RENDERPASS_MAIN:
										case RENDERPASS_PREPASS:
										case RENDERPASS_PREPASS_DEPTHONLY:
										{
											RenderPassInfo renderpass_info;
											if (renderPass == RENDERPASS_PREPASS_DEPTHONLY)
											{
												renderpass_info.rt_count = 0;
												renderpass_info.rt_formats[0] = Format::UNKNOWN;
											}
											else
											{
												renderpass_info.rt_count = 1;
												renderpass_info.rt_formats[0] = renderPass == RENDERPASS_MAIN ? format_rendertarget_main : format_idbuffer;
											}
											renderpass_info.ds_format = format_depthbuffer_main;
											const uint32_t msaa_support[] = { 1,2,4,8 };
											for (uint32_t msaa : msaa_support)
											{
												variant.bits.sample_count = msaa;
												renderpass_info.sample_count = msaa;
												device->CreatePipelineState(&desc, GetObjectPSO(variant), &renderpass_info);
											}
										}
										break;

										case RENDERPASS_ENVMAPCAPTURE:
										{
											RenderPassInfo renderpass_info;
											renderpass_info.rt_count = 1;
											renderpass_info.rt_formats[0] = format_rendertarget_envprobe;
											renderpass_info.ds_format = format_depthbuffer_envprobe;
											const uint32_t msaa_support[] = { 1,8 };
											for (uint32_t msaa : msaa_support)
											{
												variant.bits.sample_count = msaa;
												renderpass_info.sample_count = msaa;
												device->CreatePipelineState(&desc, GetObjectPSO(variant), &renderpass_info);
											}
										}
										break;

										case RENDERPASS_SHADOW:
										{
											RenderPassInfo renderpass_info;
											renderpass_info.rt_count = 1;
											renderpass_info.rt_formats[0] = format_rendertarget_shadowmap;
											renderpass_info.ds_format = format_depthbuffer_shadowmap;
											device->CreatePipelineState(&desc, GetObjectPSO(variant), &renderpass_info);
										}
										break;

										case RENDERPASS_RAINBLOCKER:
										{
											RenderPassInfo renderpass_info;
											renderpass_info.rt_count = 0;
											renderpass_info.ds_format = format_depthbuffer_shadowmap;
											device->CreatePipelineState(&desc, GetObjectPSO(variant), &renderpass_info);
										}
										break;

										default:
											device->CreatePipelineState(&desc, GetObjectPSO(variant));
											break;
										}
									}
								}
							}
						}
						});
				}
			}
		}

	}
}