#include "Renderer.h"
#include "ShaderCompiler.h"

#include "Utils/JobSystem.h"
#include "Utils/Backlog.h"
#include "Helpers.hpp"

#include <filesystem>

#ifdef SHADERDUMP_ENABLED
// Note: when using Shader Dump, use relative directory, because the dump will contain relative names too
std::string SHADERPATH = "Shaders/";
std::string SHADERSOURCEPATH = "Shaders/";
#else
// Note: when NOT using Shader Dump, use absolute directory, to avoid the case when something (eg. file dialog) overrides working directory
std::string SHADERPATH = helpers::GetCurrentPath() + "Shaders/";
std::string SHADERSOURCEPATH = helpers::GetCurrentPath() + "Shaders/";
#endif // SHADERDUMP_ENABLED

std::atomic<size_t> SHADER_ERRORS{ 0 };
std::atomic<size_t> SHADER_MISSING{ 0 };

namespace vz::graphics::common
{
	extern InputLayout			inputLayouts[ILTYPE_COUNT];
	extern RasterizerState		rasterizers[RSTYPE_COUNT];
	extern DepthStencilState	depthStencils[DSSTYPE_COUNT];
	extern BlendState			blendStates[BSTYPE_COUNT];
	extern Shader				shaders[SHADERTYPE_COUNT];
	extern GPUBuffer			buffers[BUFFERTYPE_COUNT];
	extern Sampler				samplers[SAMPLER_COUNT];

	extern PipelineState		PSO_debug[DEBUGRENDERING_COUNT];
	extern PipelineState		PSO_mesh[RENDERPASS_COUNT];
}

namespace vz::graphics::shader
{
	using namespace vz::graphics;

	GraphicsDevice*& device = GetDevice();

	bool LoadShader(
		ShaderStage stage,
		Shader& shader,
		const std::string& filename,
		ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines
	)
	{
		std::string shaderbinaryfilename = SHADERPATH + filename;
		
		// dependency naming convention
		//	e.g., ShaderInterop.h and ShaderInterop_BVH.h
		if (!permutation_defines.empty())
		{
			//std::filesystem::path file_path(filename);
			//std::string ext = file_path.extension().string();
			//shaderbinaryfilename = shaderbinaryfilename.substr(0, shaderbinaryfilename.length() - ext.size());

			std::string ext = helpers::GetExtensionFromFileName(shaderbinaryfilename);
			shaderbinaryfilename = helpers::RemoveExtension(shaderbinaryfilename);

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
			auto it = shaderdump::shadersDump.find(shaderbinaryfilename);
			if (it != shaderdump::shadersDump.end())
			{
				return device->CreateShader(stage, it->second.data, it->second.size, &shader);
			}
			else
			{
				backlog::post("shader dump doesn't contain shader: " + shaderbinaryfilename, backlog::LogLevel::Error);
			}
#endif // SHADERDUMP_ENABLED
		}
		
		shadercompiler::RegisterShader(shaderbinaryfilename);

		if (shadercompiler::IsShaderOutdated(shaderbinaryfilename))
		{
			shadercompiler::CompilerInput input;
			input.format = device->GetShaderFormat();
			input.stage = stage;
			input.minshadermodel = minshadermodel;
			input.defines = permutation_defines;

			std::string sourcedir = SHADERSOURCEPATH;
			helpers::MakePathAbsolute(sourcedir);
			input.include_directories.push_back(sourcedir);
			input.include_directories.push_back(sourcedir + helpers::GetDirectoryFromPath(filename));
			input.shadersourcefilename = helpers::ReplaceExtension(sourcedir + filename, "hlsl");

			shadercompiler::CompilerOutput output;
			shadercompiler::Compile(input, output);

			if (output.IsValid())
			{
				shadercompiler::SaveShaderAndMetadata(shaderbinaryfilename, output);

				if (!output.error_message.empty())
				{
					backlog::post(output.error_message, backlog::LogLevel::Warn);
				}
				backlog::post("shader compiled: " + shaderbinaryfilename);
				return device->CreateShader(stage, output.shaderdata, output.shadersize, &shader);
			}
			else
			{
				backlog::post("shader compile FAILED: " + shaderbinaryfilename + "\n" + output.error_message, backlog::LogLevel::Error);
				SHADER_ERRORS.fetch_add(1);
			}
		}

		if (device != nullptr)
		{
			std::vector<uint8_t> buffer;
			if (helpers::FileRead(shaderbinaryfilename, buffer))
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

	// this is for the case when retry LoadShaders() 
	jobsystem::context objectps_ctx;

	void LoadShaders()
	{
		jobsystem::Wait(objectps_ctx);
		objectps_ctx.priority = jobsystem::Priority::Low;

		jobsystem::context ctx;
		
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, common::shaders[VSTYPE_DEBUG], "meshVS_debug.cso");
			});

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, common::shaders[VSTYPE_COMMON], "meshVS_common.cso");
			});

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, common::shaders[VSTYPE_SIMPLE], "meshVS_simple.cso");
			});

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			common::inputLayouts[ILTYPE_VERTEXCOLOR].elements =
			{
				{ "POSITION", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
				{ "TEXCOORD", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
			};
			LoadShader(ShaderStage::VS, common::shaders[VSTYPE_VERTEXCOLOR], "vertexcolorVS.cso");
			});

		common::inputLayouts[ILTYPE_POSITION].elements =
		{
			{ "POSITION", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
		};
		
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, common::shaders[PSTYPE_DEBUG], "meshPS_debug.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, common::shaders[PSTYPE_SIMPLE], "meshPS_simple.cso"); });

		static const std::vector<std::string> shaderTypeDefines[] = {
			{}, // ShaderType::PHONG,
			{"PBR"}, // ShaderType::PBR,
		};
		static_assert(SHADERTYPE_BIN_COUNT == arraysize(shaderTypeDefines), "These values must match!");

		jobsystem::Dispatch(objectps_ctx, SHADERTYPE_BIN_COUNT, 1, [](jobsystem::JobArgs args) {

			LoadShader(
				ShaderStage::PS,
				common::shaders[PS_PHONG_FORWARD_BEGIN + args.jobIndex],
				"meshPS_FW.cso",
				ShaderModel::SM_6_0,
				shaderTypeDefines[args.jobIndex] // permutation defines
			);

			});

		//jobsystem::Dispatch(objectps_ctx, SHADERTYPE_BIN_COUNT, 1, [](jobsystem::JobArgs args) {
		//
		//	auto defines = shaderTypeDefines[args.jobIndex];
		//	defines.push_back("TRANSPARENT");
		//	LoadShader(
		//		ShaderStage::PS,
		//		common::shaders[PS_PHONG_FORWARD_TRANSPARENT_BEGIN + args.jobIndex],
		//		"meshPS_FW.cso",
		//		ShaderModel::SM_6_0,
		//		defines // permutation defines
		//	);
		//
		//	});
		//
		// TODO : add the deferred shaders described by PS_PHONG_TRANSPARENT_BEGIN and PS_PHONG_TRANSPARENT_TRANSPARENT_BEGIN

		jobsystem::Wait(ctx);

		// create graphics pipelines
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &common::shaders[VSTYPE_SIMPLE];
			desc.ps = &common::shaders[PSTYPE_SIMPLE];
			desc.rs = &common::rasterizers[RSTYPE_WIRE];
			desc.bs = &common::blendStates[BSTYPE_OPAQUE];
			desc.dss = &common::depthStencils[DSSTYPE_DEFAULT];

			device->CreatePipelineState(&desc, &common::PSO_mesh[RENDERPASS_FORWARD_WIRE]);

			//desc.pt = PrimitiveTopology::PATCHLIST;
			//desc.vs = &common::shaders[VSTYPE_OBJECT_SIMPLE_TESSELLATION];
			//desc.hs = &common::shaders[HSTYPE_OBJECT_SIMPLE];
			//desc.ds = &common::shaders[DSTYPE_OBJECT_SIMPLE];
			//device->CreatePipelineState(&desc, &PSO_object_wire_tessellation);
			});

		//jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
		//	PipelineStateDesc desc;
		//	desc.vs = &common::shaders[VSTYPE_POSTPROCESS];
		//	desc.ps = &common::shaders[PSTYPE_POSTPROCESS_OUTLINE];
		//	desc.rs = &common::rasterizers[RSTYPE_DOUBLESIDED];
		//	desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
		//	desc.dss = &common::depthStencils[DSSTYPE_DEPTHDISABLED];
		//
		//	device->CreatePipelineState(&desc, &PSO_outline);
		//	});

		jobsystem::Dispatch(ctx, DEBUGRENDERING_COUNT, 1, [](jobsystem::JobArgs args) {
			PipelineStateDesc desc;

			switch (args.jobIndex)
			{
			case DEBUGRENDERING_GRID:
				desc.vs = &common::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &common::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &common::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &common::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_CUBE:
				desc.vs = &common::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &common::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &common::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &common::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_CUBE_DEPTH:
				desc.vs = &common::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &common::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &common::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &common::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_LINES:
				desc.vs = &common::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &common::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &common::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &common::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_LINES_DEPTH:
				desc.vs = &common::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &common::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &common::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &common::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_SOLID:
				desc.vs = &common::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &common::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &common::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &common::rasterizers[RSTYPE_DOUBLESIDED];
				desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_WIREFRAME:
				desc.vs = &common::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &common::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &common::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &common::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_SOLID_DEPTH:
				desc.vs = &common::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &common::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &common::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &common::rasterizers[RSTYPE_DOUBLESIDED];
				desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_WIREFRAME_DEPTH:
				desc.vs = &common::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &common::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &common::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &common::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_EMITTER:
				desc.vs = &common::shaders[VSTYPE_DEBUG];
				desc.ps = &common::shaders[PSTYPE_DEBUG];
				desc.dss = &common::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &common::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &common::blendStates[BSTYPE_OPAQUE];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			//case DEBUGRENDERING_RAYTRACE_BVH:
			//	desc.vs = &common::shaders[VSTYPE_RAYTRACE_SCREEN];
			//	desc.ps = &common::shaders[PSTYPE_RAYTRACE_DEBUGBVH];
			//	desc.dss = &common::depthStencils[DSSTYPE_DEPTHDISABLED];
			//	desc.rs = &common::rasterizers[RSTYPE_DOUBLESIDED];
			//	desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
			//	desc.pt = PrimitiveTopology::TRIANGLELIST;
			//	break;
			}

			device->CreatePipelineState(&desc, &common::PSO_debug[args.jobIndex]);
			});
			/*

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
			desc.vs = &common::shaders[realVS];
			desc.ps = &common::shaders[PSTYPE_OBJECT_HOLOGRAM];

			desc.bs = &common::blendStates[BSTYPE_ADDITIVE];
			desc.rs = &common::rasterizers[RSTYPE_FRONT];
			desc.dss = &common::depthStencils[DSSTYPE_HOLOGRAM];
			desc.pt = PrimitiveTopology::TRIANGLELIST;

			PipelineState pso;
			device->CreatePipelineState(&desc, &pso);

			CustomShader customShader;
			customShader.name = "Hologram";
			customShader.filterMask = FILTER_TRANSPARENT;
			customShader.pso[RENDERPASS_MAIN] = pso;
			RegisterCustomShader(customShader);
		}


		jobsystem::Wait(ctx);



		for (uint32_t renderPass = 0; renderPass < RENDERPASS_COUNT; ++renderPass)
		{
			for (uint32_t mesh_shader = 0; mesh_shader <= (device->CheckCapability(GraphicsDeviceCapability::MESH_SHADER) ? 1u : 0u); ++mesh_shader)
			{
				// default objectshaders:
				//	We don't wait for these here, because then it can slow down the init time a lot
				//	We will wait for these to complete in RenderMeshes() just before they will be first used
				jobsystem::Wait(object_pso_job_ctx[renderPass][mesh_shader]);
				object_pso_job_ctx[renderPass][mesh_shader].priority = jobsystem::Priority::Low;
				for (uint32_t shaderType = 0; shaderType < MaterialComponent::SHADERTYPE_COUNT; ++shaderType)
				{
					jobsystem::Execute(object_pso_job_ctx[renderPass][mesh_shader], [=](jobsystem::JobArgs args) {
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
											desc.as = realAS < SHADERTYPE_COUNT ? &common::shaders[realAS] : nullptr;
											desc.ms = realMS < SHADERTYPE_COUNT ? &common::shaders[realMS] : nullptr;
										}
										else
										{
											SHADERTYPE realVS = GetVSTYPE((RENDERPASS)renderPass, tessellation, alphatest, transparency);
											SHADERTYPE realHS = GetHSTYPE((RENDERPASS)renderPass, tessellation, alphatest);
											SHADERTYPE realDS = GetDSTYPE((RENDERPASS)renderPass, tessellation, alphatest);
											SHADERTYPE realGS = GetGSTYPE((RENDERPASS)renderPass, alphatest, transparency);

											if (tessellation && (realHS == SHADERTYPE_COUNT || realDS == SHADERTYPE_COUNT))
												continue;

											desc.vs = realVS < SHADERTYPE_COUNT ? &common::shaders[realVS] : nullptr;
											desc.hs = realHS < SHADERTYPE_COUNT ? &common::shaders[realHS] : nullptr;
											desc.ds = realDS < SHADERTYPE_COUNT ? &common::shaders[realDS] : nullptr;
											desc.gs = realGS < SHADERTYPE_COUNT ? &common::shaders[realGS] : nullptr;
										}

										SHADERTYPE realPS = GetPSTYPE((RENDERPASS)renderPass, alphatest, transparency, (MaterialComponent::SHADERTYPE)shaderType);
										desc.ps = realPS < SHADERTYPE_COUNT ? &common::shaders[realPS] : nullptr;

										switch (blendMode)
										{
										case BLENDMODE_OPAQUE:
											desc.bs = &common::blendStates[BSTYPE_OPAQUE];
											break;
										case BLENDMODE_ALPHA:
											desc.bs = &common::blendStates[BSTYPE_TRANSPARENT];
											break;
										case BLENDMODE_ADDITIVE:
											desc.bs = &common::blendStates[BSTYPE_ADDITIVE];
											break;
										case BLENDMODE_PREMULTIPLIED:
											desc.bs = &common::blendStates[BSTYPE_PREMULTIPLIED];
											break;
										case BLENDMODE_MULTIPLY:
											desc.bs = &common::blendStates[BSTYPE_MULTIPLY];
											break;
										default:
											assert(0);
											break;
										}

										switch (renderPass)
										{
										case RENDERPASS_SHADOW:
											desc.bs = &common::blendStates[transparency ? BSTYPE_TRANSPARENTSHADOW : BSTYPE_COLORWRITEDISABLE];
											break;
										case RENDERPASS_RAINBLOCKER:
											desc.bs = &common::blendStates[BSTYPE_COLORWRITEDISABLE];
											break;
										default:
											break;
										}

										switch (renderPass)
										{
										case RENDERPASS_SHADOW:
											desc.dss = &common::depthStencils[transparency ? DSSTYPE_DEPTHREAD : DSSTYPE_SHADOW];
											break;
										case RENDERPASS_MAIN:
											if (blendMode == BLENDMODE_ADDITIVE)
											{
												desc.dss = &common::depthStencils[DSSTYPE_DEPTHREAD];
											}
											else
											{
												desc.dss = &common::depthStencils[transparency ? DSSTYPE_TRANSPARENT : DSSTYPE_DEPTHREADEQUAL];
											}
											break;
										case RENDERPASS_ENVMAPCAPTURE:
											desc.dss = &common::depthStencils[DSSTYPE_ENVMAP];
											break;
										case RENDERPASS_VOXELIZE:
											desc.dss = &common::depthStencils[DSSTYPE_DEPTHDISABLED];
											break;
										case RENDERPASS_RAINBLOCKER:
											desc.dss = &common::depthStencils[DSSTYPE_DEFAULT];
											break;
										default:
											if (blendMode == BLENDMODE_ADDITIVE)
											{
												desc.dss = &common::depthStencils[DSSTYPE_DEPTHREAD];
											}
											else
											{
												desc.dss = &common::depthStencils[DSSTYPE_DEFAULT];
											}
											break;
										}

										switch (renderPass)
										{
										case RENDERPASS_SHADOW:
											desc.rs = &common::rasterizers[cullMode == (int)CullMode::NONE ? RSTYPE_SHADOW_DOUBLESIDED : RSTYPE_SHADOW];
											break;
										case RENDERPASS_VOXELIZE:
											desc.rs = &common::rasterizers[RSTYPE_VOXELIZE];
											break;
										default:
											switch ((CullMode)cullMode)
											{
											default:
											case CullMode::BACK:
												desc.rs = &common::rasterizers[RSTYPE_FRONT];
												break;
											case CullMode::NONE:
												desc.rs = &common::rasterizers[RSTYPE_DOUBLESIDED];
												break;
											case CullMode::FRONT:
												desc.rs = &common::rasterizers[RSTYPE_BACK];
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

										jobsystem::Wait(objectps_ctx);
										if (mesh_shader)
										{
											jobsystem::Wait(mesh_shader_ctx);
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
		/**/
	}
}