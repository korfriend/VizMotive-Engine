#include "PluginInterface.h"
#include "Renderer.h"
#include "ShaderCompiler.h"

#include "Utils/JobSystem.h"
#include "Utils/Backlog.h"
#include "Utils/Helpers.h"

#include <unordered_map>

#ifdef SHADERDUMP_ENABLED
// Note: when using Shader Dump, use relative directory, because the dump will contain relative names too
std::string SHADERPATH = "./Shaders/";
std::string SHADERSOURCEPATH = "./Shaders/";
#else
// Note: when NOT using Shader Dump, use absolute directory, to avoid the case when something (eg. file dialog) overrides working directory
std::string SHADERPATH = vz::helper::GetCurrentPath() + "/Shaders/";
std::string SHADERSOURCEPATH = vz::helper::GetCurrentPath() + "../../../EnginePlugins/RendererDX12/Shaders/";
#endif // SHADERDUMP_ENABLED

std::atomic<size_t> SHADER_ERRORS{ 0 };
std::atomic<size_t> SHADER_MISSING{ 0 };

namespace vz::rcommon
{
	extern InputLayout			inputLayouts[ILTYPE_COUNT];
	extern RasterizerState		rasterizers[RSTYPE_COUNT];
	extern DepthStencilState	depthStencils[DSSTYPE_COUNT];
	extern BlendState			blendStates[BSTYPE_COUNT];
	extern Shader				shaders[SHADERTYPE_COUNT];
	extern GPUBuffer			buffers[BUFFERTYPE_COUNT];
	extern Sampler				samplers[SAMPLER_COUNT];

	extern PipelineState		PSO_debug[DEBUGRENDERING_COUNT];
	extern PipelineState		PSO_render[RENDERPASS_COUNT];
	extern PipelineState		PSO_wireframe;
	extern PipelineState		PSO_occlusionquery;

	extern jobsystem::context	CTX_renderPSO[RENDERPASS_COUNT][MESH_SHADER_PSO_COUNT];
}


namespace vz
{
	bool LoadShader(
		graphics::ShaderStage stage,
		graphics::Shader& shader,
		const std::string& filename,
		graphics::ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines)
	{
		return shader::LoadShader(stage, shader, filename, minshadermodel, permutation_defines);
	}
	bool LoadShaders()
	{
		shader::LoadShaders();
		return true;
	}
}

namespace vz::shader
{
	using namespace vz::graphics;

	GraphicsDevice*& device = GetDevice();
	// this is for the case when retry LoadShaders() 
	jobsystem::context CTX_renderPS;
	jobsystem::context CTX_renderMS;

	SHADERTYPE GetASTYPE(RENDERPASS renderPass, bool tessellation, bool alphatest, bool transparent, bool mesh_shader)
	{
		if (!mesh_shader)
			return SHADERTYPE_COUNT;

		return ASTYPE_MESH;
	}
	SHADERTYPE GetMSTYPE(RENDERPASS renderPass, bool tessellation, bool alphatest, bool transparent, bool mesh_shader)
	{
		if (!mesh_shader)
			return SHADERTYPE_COUNT;

		SHADERTYPE realMS = SHADERTYPE_COUNT;

		switch (renderPass)
		{
		case RENDERPASS_MAIN:
			realMS = MSTYPE_MESH;
			break;
		case RENDERPASS_PREPASS:
		case RENDERPASS_PREPASS_DEPTHONLY:
			if (alphatest)
			{
				realMS = MSTYPE_MESH_PREPASS_ALPHATEST;
			}
			else
			{
				realMS = MSTYPE_MESH_PREPASS;
			}
			break;
		case RENDERPASS_SHADOW:
			if (transparent)
			{
				realMS = MSTYPE_SHADOW_TRANSPARENT;
			}
			else
			{
				if (alphatest)
				{
					realMS = MSTYPE_SHADOW_ALPHATEST;
				}
				else
				{
					realMS = MSTYPE_SHADOW;
				}
			}
			break;
		}

		return realMS;
	}
	SHADERTYPE GetVSTYPE(RENDERPASS renderPass, bool tessellation, bool alphatest, bool transparent)
	{
		SHADERTYPE realVS = VSTYPE_MESH_SIMPLE;

		switch (renderPass)
		{
		case RENDERPASS_MAIN:
			if (tessellation)
			{
				realVS = VSTYPE_MESH_COMMON_TESSELLATION;
			}
			else
			{
				realVS = VSTYPE_MESH_COMMON;
			}
			break;
		case RENDERPASS_PREPASS:
		case RENDERPASS_PREPASS_DEPTHONLY:
			if (tessellation)
			{
				if (alphatest)
				{
					realVS = VSTYPE_MESH_PREPASS_ALPHATEST_TESSELLATION;
				}
				else
				{
					realVS = VSTYPE_MESH_PREPASS_TESSELLATION;
				}
			}
			else
			{
				if (alphatest)
				{
					realVS = VSTYPE_MESH_PREPASS_ALPHATEST;
				}
				else
				{
					realVS = VSTYPE_MESH_PREPASS;
				}
			}
			break;
		case RENDERPASS_ENVMAPCAPTURE:
			realVS = VSTYPE_ENVMAP;
			break;
		case RENDERPASS_SHADOW:
			if (transparent)
			{
				realVS = VSTYPE_SHADOW_TRANSPARENT;
			}
			else
			{
				if (alphatest)
				{
					realVS = VSTYPE_SHADOW_ALPHATEST;
				}
				else
				{
					realVS = VSTYPE_SHADOW;
				}
			}
			break;
		case RENDERPASS_VOXELIZE:
			realVS = VSTYPE_VOXELIZER;
			break;
		}

		return realVS;
	}
	SHADERTYPE GetGSTYPE(RENDERPASS renderPass, bool alphatest, bool transparent)
	{
		SHADERTYPE realGS = SHADERTYPE_COUNT;

		switch (renderPass)
		{
#ifdef VOXELIZATION_GEOMETRY_SHADER_ENABLED
		case RENDERPASS_VOXELIZE:
			realGS = GSTYPE_VOXELIZER;
			break;
#endif // VOXELIZATION_GEOMETRY_SHADER_ENABLED
		case RENDERPASS_ENVMAPCAPTURE:
			if (device->CheckCapability(GraphicsDeviceCapability::RENDERTARGET_AND_VIEWPORT_ARRAYINDEX_WITHOUT_GS))
				break;
			realGS = GSTYPE_ENVMAP_EMULATION;
			break;
		case RENDERPASS_SHADOW:
			if (device->CheckCapability(GraphicsDeviceCapability::RENDERTARGET_AND_VIEWPORT_ARRAYINDEX_WITHOUT_GS))
				break;
			if (transparent)
			{
				realGS = GSTYPE_SHADOW_TRANSPARENT_EMULATION;
			}
			else
			{
				if (alphatest)
				{
					realGS = GSTYPE_SHADOW_ALPHATEST_EMULATION;
				}
				else
				{
					realGS = GSTYPE_SHADOW_EMULATION;
				}
			}
			break;
		}

		return realGS;
	}
	SHADERTYPE GetHSTYPE(RENDERPASS renderPass, bool tessellation, bool alphatest)
	{
		if (tessellation)
		{
			switch (renderPass)
			{
			case RENDERPASS_PREPASS:
			case RENDERPASS_PREPASS_DEPTHONLY:
				if (alphatest)
				{
					return HSTYPE_MESH_PREPASS_ALPHATEST;
				}
				else
				{
					return HSTYPE_MESH_PREPASS;
				}
				break;
			case RENDERPASS_MAIN:
				return HSTYPE_MESH;
				break;
			}
		}

		return SHADERTYPE_COUNT;
	}
	SHADERTYPE GetDSTYPE(RENDERPASS renderPass, bool tessellation, bool alphatest)
	{
		if (tessellation)
		{
			switch (renderPass)
			{
			case RENDERPASS_PREPASS:
			case RENDERPASS_PREPASS_DEPTHONLY:
				if (alphatest)
				{
					return DSTYPE_MESH_PREPASS_ALPHATEST;
				}
				else
				{
					return DSTYPE_MESH_PREPASS;
				}
			case RENDERPASS_MAIN:
				return DSTYPE_MESH;
			}
		}

		return SHADERTYPE_COUNT;
	}
	SHADERTYPE GetPSTYPE(RENDERPASS renderPass, bool deferred, bool alphatest, bool transparent, MaterialComponent::ShaderType shaderType)
	{
		SHADERTYPE realPS = SHADERTYPE_COUNT;

		uint32_t index_material_shadertype = SCU32(shaderType);
		switch (renderPass)
		{
		case RENDERPASS_MAIN:
			if (deferred)
			{
				realPS = SHADERTYPE((transparent ? PSTYPE_RENDERABLE_TRANSPARENT_PERMUTATION__BEGIN : PSTYPE_RENDERABLE_PERMUTATION__BEGIN) + index_material_shadertype);
			}
			else
			{
				realPS = SHADERTYPE((transparent ? PSTYPE_RENDERABLE_TRANSPARENT_PERMUTATION__BEGIN : PSTYPE_RENDERABLE_PERMUTATION__BEGIN) + index_material_shadertype);
			}
			break;
		case RENDERPASS_PREPASS:
			if (alphatest)
			{
				realPS = PSTYPE_MESH_PREPASS_ALPHATEST;
			}
			else
			{
				realPS = PSTYPE_MESH_PREPASS;
			}
			break;
		case RENDERPASS_PREPASS_DEPTHONLY:
			if (alphatest)
			{
				realPS = PSTYPE_MESH_PREPASS_DEPTHONLY_ALPHATEST;
			}
			else
			{
				realPS = PSTYPE_MESH_PREPASS_DEPTHONLY;
			}
			break;
		case RENDERPASS_ENVMAPCAPTURE:
			realPS = PSTYPE_ENVMAP;
			break;
		case RENDERPASS_SHADOW:
			if (transparent)
			{
				realPS = PSTYPE_SHADOW_TRANSPARENT;
			}
			else
			{
				if (alphatest)
				{
					realPS = PSTYPE_SHADOW_ALPHATEST;
				}
				else
				{
					realPS = SHADERTYPE_COUNT;
				}
			}
			break;
		case RENDERPASS_VOXELIZE:
			realPS = PSTYPE_VOXELIZER;
			break;
		default:
			break;
		}

		return realPS;
	}

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

			std::string ext = vz::helper::GetExtensionFromFileName(shaderbinaryfilename);
			shaderbinaryfilename = vz::helper::RemoveExtension(shaderbinaryfilename);

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
			vz::helper::MakePathAbsolute(sourcedir);
			input.include_directories.push_back(sourcedir);
			input.include_directories.push_back(sourcedir + vz::helper::GetDirectoryFromPath(filename));
			input.shadersourcefilename = vz::helper::ReplaceExtension(sourcedir + filename, "hlsl");

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
			if (vz::helper::FileRead(shaderbinaryfilename, buffer))
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


	std::unordered_map<uint32_t, PipelineState> PSO_object[RENDERPASS_COUNT][SHADERTYPE_BIN_COUNT];
	PipelineState* GetObjectPSO(MeshRenderingVariant variant)
	{
		return &PSO_object[variant.bits.renderpass][variant.bits.shadertype][variant.value];
	}

	void LoadShaders()
	{
		jobsystem::Wait(CTX_renderPS);
		CTX_renderPS.priority = jobsystem::Priority::Low;

		jobsystem::context ctx;
		
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, rcommon::shaders[VSTYPE_MESH_DEBUG], "meshVS_debug.cso");
			});

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, rcommon::shaders[VSTYPE_MESH_COMMON], "meshVS_common.cso");
			});

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			LoadShader(ShaderStage::VS, rcommon::shaders[VSTYPE_MESH_SIMPLE], "meshVS_simple.cso");
			});

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			rcommon::inputLayouts[ILTYPE_VERTEXCOLOR].elements =
			{
				{ "POSITION", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
				{ "TEXCOORD", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
			};
			LoadShader(ShaderStage::VS, rcommon::shaders[VSTYPE_VERTEXCOLOR], "vertexcolorVS.cso");
			});

		rcommon::inputLayouts[ILTYPE_POSITION].elements =
		{
			{ "POSITION", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
		};
		
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, rcommon::shaders[PSTYPE_DEBUG], "meshPS_debug.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, rcommon::shaders[PSTYPE_SIMPLE], "meshPS_simple.cso"); });

		assert(0 && "OKAY HERE");
		return;

		static const std::vector<std::string> shaderTypeDefines[] = {
			{"PHONG"}, // ShaderType::PHONG,
			{"PBR"}, // ShaderType::PBR,
			{"UNLIT"}, // ShaderType::UNLIT,
		};
		static_assert(SHADERTYPE_BIN_COUNT == arraysize(shaderTypeDefines), "These values must match!");

		jobsystem::Dispatch(CTX_renderPS, SHADERTYPE_BIN_COUNT, 1, [](jobsystem::JobArgs args) {

			LoadShader(
				ShaderStage::PS,
				rcommon::shaders[PSTYPE_RENDERABLE_PERMUTATION__BEGIN + args.jobIndex],
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
			desc.vs = &rcommon::shaders[VSTYPE_MESH_SIMPLE];
			desc.ps = &rcommon::shaders[PSTYPE_SIMPLE];
			desc.rs = &rcommon::rasterizers[RSTYPE_WIRE];
			desc.bs = &rcommon::blendStates[BSTYPE_OPAQUE];
			desc.dss = &rcommon::depthStencils[DSSTYPE_DEFAULT];

			device->CreatePipelineState(&desc, &rcommon::PSO_wireframe);

			//desc.pt = PrimitiveTopology::PATCHLIST;
			//desc.vs = &common::shaders[VSTYPE_OBJECT_SIMPLE_TESSELLATION];
			//desc.hs = &common::shaders[HSTYPE_MESH_SIMPLE];
			//desc.ds = &common::shaders[DSTYPE_MESH_SIMPLE];
			//device->CreatePipelineState(&desc, &PSO_object_wire_tessellation);
			});


		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &rcommon::shaders[VSTYPE_OCCLUDEE];
			desc.rs = &rcommon::rasterizers[RSTYPE_OCCLUDEE];
			desc.bs = &rcommon::blendStates[BSTYPE_COLORWRITEDISABLE];
			desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHREAD];
			desc.pt = PrimitiveTopology::TRIANGLESTRIP;

			device->CreatePipelineState(&desc, &rcommon::PSO_occlusionquery);
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
				desc.vs = &rcommon::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &rcommon::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &rcommon::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_CUBE:
				desc.vs = &rcommon::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &rcommon::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &rcommon::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_CUBE_DEPTH:
				desc.vs = &rcommon::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &rcommon::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &rcommon::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_LINES:
				desc.vs = &rcommon::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &rcommon::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &rcommon::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_LINES_DEPTH:
				desc.vs = &rcommon::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &rcommon::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &rcommon::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_SOLID:
				desc.vs = &rcommon::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &rcommon::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &rcommon::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rcommon::rasterizers[RSTYPE_DOUBLESIDED];
				desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_WIREFRAME:
				desc.vs = &rcommon::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &rcommon::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &rcommon::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_SOLID_DEPTH:
				desc.vs = &rcommon::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &rcommon::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &rcommon::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rcommon::rasterizers[RSTYPE_DOUBLESIDED];
				desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_TRIANGLE_WIREFRAME_DEPTH:
				desc.vs = &rcommon::shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &rcommon::shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &rcommon::inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::TRIANGLELIST;
				break;
			case DEBUGRENDERING_EMITTER:
				desc.vs = &rcommon::shaders[VSTYPE_MESH_DEBUG];
				desc.ps = &rcommon::shaders[PSTYPE_DEBUG];
				desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &rcommon::blendStates[BSTYPE_OPAQUE];
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

			device->CreatePipelineState(&desc, &rcommon::PSO_debug[args.jobIndex]);
		});
		
		jobsystem::Wait(ctx);

		const uint32_t renderPass = 0;
		//for (uint32_t renderPass = 0; renderPass < RENDERPASS_COUNT; ++renderPass)
		{
			const uint32_t mesh_shader = 0;
			//for (uint32_t mesh_shader = 0; mesh_shader <= (device->CheckCapability(GraphicsDeviceCapability::MESH_SHADER) ? 1u : 0u); ++mesh_shader)
			{
				// default objectshaders:
				//	We don't wait for these here, because then it can slow down the init time a lot
				//	We will wait for these to complete in RenderMeshes() just before they will be first used
				jobsystem::Wait(rcommon::CTX_renderPSO[renderPass][mesh_shader]);
				rcommon::CTX_renderPSO[renderPass][mesh_shader].priority = jobsystem::Priority::Low;
				for (uint32_t shaderType = 0; shaderType < SHADERTYPE_BIN_COUNT; ++shaderType)
				{
					jobsystem::Execute(rcommon::CTX_renderPSO[renderPass][mesh_shader], [=](jobsystem::JobArgs args) {
						for (uint32_t blendMode = 0; blendMode < BLENDMODE_COUNT; ++blendMode)
						{
							for (uint32_t cullMode = 0; cullMode <= 3; ++cullMode) // graphics::CullMode (NONE, FRONT, BACK)
							{
								const uint32_t tesselation_enabled = 0; // (TODO) 1
								for (uint32_t tessellation = 0; tessellation <= tesselation_enabled; ++tessellation)
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
											desc.as = realAS < SHADERTYPE_COUNT ? &rcommon::shaders[realAS] : nullptr;
											desc.ms = realMS < SHADERTYPE_COUNT ? &rcommon::shaders[realMS] : nullptr;
										}
										else
										{
											SHADERTYPE realVS = GetVSTYPE((RENDERPASS)renderPass, tessellation, alphatest, transparency);
											SHADERTYPE realHS = GetHSTYPE((RENDERPASS)renderPass, tessellation, alphatest);
											SHADERTYPE realDS = GetDSTYPE((RENDERPASS)renderPass, tessellation, alphatest);
											SHADERTYPE realGS = GetGSTYPE((RENDERPASS)renderPass, alphatest, transparency);

											if (tessellation && (realHS == SHADERTYPE_COUNT || realDS == SHADERTYPE_COUNT))
												continue;

											desc.vs = realVS < SHADERTYPE_COUNT ? &rcommon::shaders[realVS] : nullptr;
											desc.hs = realHS < SHADERTYPE_COUNT ? &rcommon::shaders[realHS] : nullptr;
											desc.ds = realDS < SHADERTYPE_COUNT ? &rcommon::shaders[realDS] : nullptr;
											desc.gs = realGS < SHADERTYPE_COUNT ? &rcommon::shaders[realGS] : nullptr;
										}

										const uint32_t deferred_enabled = 0; // (TODO) 1

										SHADERTYPE realPS = GetPSTYPE((RENDERPASS)renderPass, deferred_enabled, alphatest, transparency, static_cast<MaterialComponent::ShaderType>(shaderType));
										desc.ps = realPS < SHADERTYPE_COUNT ? &rcommon::shaders[realPS] : nullptr;

										switch (blendMode)
										{
										case BLENDMODE_OPAQUE:
											desc.bs = &rcommon::blendStates[BSTYPE_OPAQUE];
											break;
										case BLENDMODE_ALPHA:
											desc.bs = &rcommon::blendStates[BSTYPE_TRANSPARENT];
											break;
										case BLENDMODE_ADDITIVE:
											desc.bs = &rcommon::blendStates[BSTYPE_ADDITIVE];
											break;
										case BLENDMODE_PREMULTIPLIED:
											desc.bs = &rcommon::blendStates[BSTYPE_PREMULTIPLIED];
											break;
										case BLENDMODE_MULTIPLY:
											desc.bs = &rcommon::blendStates[BSTYPE_MULTIPLY];
											break;
										default:
											assert(0);
											break;
										}

										switch (renderPass)
										{
										case RENDERPASS_SHADOW:
											desc.bs = &rcommon::blendStates[transparency ? BSTYPE_TRANSPARENTSHADOW : BSTYPE_COLORWRITEDISABLE];
											break;
										default:
											break;
										}

										switch (renderPass)
										{
										case RENDERPASS_SHADOW:
											desc.dss = &rcommon::depthStencils[transparency ? DSSTYPE_DEPTHREAD : DSSTYPE_SHADOW];
											break;
										case RENDERPASS_MAIN:
											if (blendMode == BLENDMODE_ADDITIVE)
											{
												desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHREAD];
											}
											else
											{
												desc.dss = &rcommon::depthStencils[transparency ? DSSTYPE_TRANSPARENT : DSSTYPE_DEPTHREADEQUAL];
											}
											break;
										case RENDERPASS_ENVMAPCAPTURE:
											desc.dss = &rcommon::depthStencils[DSSTYPE_ENVMAP];
											break;
										case RENDERPASS_VOXELIZE:
											desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHDISABLED];
											break;
										default:
											if (blendMode == BLENDMODE_ADDITIVE)
											{
												desc.dss = &rcommon::depthStencils[DSSTYPE_DEPTHREAD];
											}
											else
											{
												desc.dss = &rcommon::depthStencils[DSSTYPE_DEFAULT];
											}
											break;
										}

										switch (renderPass)
										{
										case RENDERPASS_SHADOW:
											desc.rs = &rcommon::rasterizers[cullMode == (int)CullMode::NONE ? RSTYPE_SHADOW_DOUBLESIDED : RSTYPE_SHADOW];
											break;
										case RENDERPASS_VOXELIZE:
											desc.rs = &rcommon::rasterizers[RSTYPE_VOXELIZE];
											break;
										default:
											switch ((CullMode)cullMode)
											{
											default:
											case CullMode::BACK:
												desc.rs = &rcommon::rasterizers[RSTYPE_FRONT];
												break;
											case CullMode::NONE:
												desc.rs = &rcommon::rasterizers[RSTYPE_DOUBLESIDED];
												break;
											case CullMode::FRONT:
												desc.rs = &rcommon::rasterizers[RSTYPE_BACK];
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

										jobsystem::Wait(CTX_renderPS);
										if (mesh_shader)
										{
											jobsystem::Wait(CTX_renderMS);
										}

										MeshRenderingVariant variant = {};
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
												renderpass_info.rt_formats[0] = renderPass == RENDERPASS_MAIN ? FORMAT_rendertargetMain : FORMAT_idbuffer;
											}
											renderpass_info.ds_format = FORMAT_depthbufferMain;
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
											renderpass_info.rt_formats[0] = FORMAT_rendertargetEnvprobe;
											renderpass_info.ds_format = FORMAT_depthbufferEnvprobe;
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
											renderpass_info.rt_formats[0] = FORMAT_rendertargetShadowmap;
											renderpass_info.ds_format = FORMAT_depthbufferShadowmap;
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