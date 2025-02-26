#include "ShaderEngine.h"
#include "Renderer.h"
#include "ShaderLoader.h"
#include "ShaderCompiler.h"
#include "RenderPath3D_Detail.h"

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
std::string SHADERSOURCEPATH = vz::helper::GetCurrentPath() + "../../../EngineShaders/Shaders/";
#endif // SHADERDUMP_ENABLED

std::atomic<size_t> SHADER_ERRORS{ 0 };
std::atomic<size_t> SHADER_MISSING{ 0 };

using namespace vz::renderer;

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
namespace vz::renderer
{
	jobsystem::context	CTX_renderPSO[RENDERPASS_COUNT][MESH_SHADER_PSO_COUNT];	// shaders

	InputLayout			inputLayouts[ILTYPE_COUNT];		// shaders
	Shader				shaders[SHADERTYPE_COUNT];		// shaders

	std::unordered_map<uint32_t, PipelineState> PSO_render[RENDERPASS_COUNT][SHADERTYPE_BIN_COUNT];	// shaders
	PipelineState		PSO_wireframe;											// shaders
	PipelineState		PSO_occlusionquery;										// shaders
	PipelineState		PSO_RenderableShapes[SHAPE_RENDERING_COUNT];			// shaders

	PipelineState* GetObjectPSO(MeshRenderingVariant variant)
	{
		return &PSO_render[variant.bits.renderpass][variant.bits.shadertype][variant.value];
	}
}

namespace vz::shader
{
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
		if (device == nullptr)
		{
			return false;
		}

		std::string shaderbinaryfilename = SHADERPATH + filename;
		std::string shadersourcepath = SHADERSOURCEPATH;
		switch (stage)
		{
		case vz::graphics::ShaderStage::MS: shadersourcepath += "MS/"; break;
		case vz::graphics::ShaderStage::AS: shadersourcepath += "AS/"; break;
		case vz::graphics::ShaderStage::VS: shadersourcepath += "VS/"; break;
		case vz::graphics::ShaderStage::HS: shadersourcepath += "HS/"; break;
		case vz::graphics::ShaderStage::DS: shadersourcepath += "DS/"; break;
		case vz::graphics::ShaderStage::GS: shadersourcepath += "GS/"; break;
		case vz::graphics::ShaderStage::PS: shadersourcepath += "PS/"; break;
		case vz::graphics::ShaderStage::CS: shadersourcepath += "CS/"; break;
		default:
			assert(0);
			return false;
		}
		
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
		
		shadercompiler::RegisterShader(shaderbinaryfilename);

		if (shadercompiler::IsShaderOutdated(shaderbinaryfilename))
		{
			shadercompiler::CompilerInput input;
			input.format = device->GetShaderFormat();
			input.stage = stage;
			input.minshadermodel = minshadermodel;
			input.defines = permutation_defines;

			std::string sourcedir = shadersourcepath;
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

	void Initialize()
	{
		LoadShaders();
	}

	void Deinitialize()
	{
		ReleaseRenderRes(renderer::shaders, SHADERTYPE_COUNT);

		renderer::PSO_wireframe = {};
		renderer::PSO_occlusionquery = {};
		ReleaseRenderRes(renderer::PSO_RenderableShapes, SHAPE_RENDERING_COUNT);

		for (size_t i = 0, n = (size_t)RENDERPASS_COUNT; i < n; ++i)
		{
			for (size_t j = 0; j < (size_t)SHADERTYPE_BIN_COUNT; ++j)
			{
				PSO_render[i][j].clear();
			}
		}
	}

	void LoadShaders()
	{
		// check shader interop check
		{
			uint64_t headerTime = {};
			helper::GetFileNamesInDirectory(SHADERSOURCEPATH, 
				[&](std::string fileName) {
					//std::cout << "Found file: " << fileName << std::endl;
					headerTime = std::max(vz::helper::FileTimestamp(fileName), headerTime);
				}, "h");

			shadercompiler::SetRecentHeaderTimeStamp(headerTime);
		}

		// naming convention based on Wicked Engine 
		//	our variants: 'object' to 'mesh', and 'visibility' to 'view'

		jobsystem::Wait(CTX_renderPS);
		CTX_renderPS.priority = jobsystem::Priority::Low;
		for (uint32_t renderPass = 0; renderPass < RENDERPASS_COUNT; ++renderPass)
		{
			for (uint32_t mesh_shader = 0; mesh_shader < MESH_SHADER_PSO_COUNT; ++mesh_shader)
			{
				jobsystem::Wait(CTX_renderPSO[renderPass][mesh_shader]);
			}
		}
		SHADER_ERRORS.store(0);
		SHADER_MISSING.store(0);

		jobsystem::context ctx;
		
		//----- Input Layers -----
		{
			inputLayouts[ILTYPE_VERTEXCOLOR].elements =
			{
				{ "POSITION", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
				{ "TEXCOORD", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
			};
			inputLayouts[ILTYPE_POSITION].elements =
			{
				{ "POSITION", 0, Format::R32G32B32A32_FLOAT, 0, InputLayout::APPEND_ALIGNED_ELEMENT, InputClassification::PER_VERTEX_DATA },
			};
		}

		//----- VS -----
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_MESH_DEBUG], "meshVS_debug.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_MESH_COMMON], "meshVS_common.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_MESH_SIMPLE], "meshVS_simple.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_MESH_PREPASS], "meshVS_prepass.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_MESH_PREPASS_ALPHATEST], "meshVS_prepass_alphatest.cso"); });

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_VERTEXCOLOR], "vertexcolorVS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::VS, shaders[VSTYPE_OCCLUDEE], "occludeeVS.cso"); });

		//----- PS -----
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_MESH_DEBUG], "meshPS_debug.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_MESH_SIMPLE], "meshPS_simple.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_VERTEXCOLOR], "vertexcolorPS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_MESH_PREPASS], "meshPS_prepass.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_MESH_PREPASS_ALPHATEST], "meshPS_prepass_alphatest.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_MESH_PREPASS_DEPTHONLY], "meshPS_prepass_depthonly.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::PS, shaders[PSTYPE_MESH_PREPASS_DEPTHONLY_ALPHATEST], "meshPS_prepass_depthonly_alphatest.cso"); });

		//----- PS materials by permutation -----
		static const std::vector<std::string> shaderTypeDefines[] = {
			{"PHONG"}, // ShaderType::PHONG,
			{"PBR"}, // ShaderType::PBR,
			{"UNLIT"}, // ShaderType::UNLIT,
		};
		//inline static const std::vector<std::string> shaderTypeDefines[] = {
		//	{}, // SHADERTYPE_PBR,
		//	{"PLANARREFLECTION"}, // SHADERTYPE_PBR_PLANARREFLECTION,
		//	{"PARALLAXOCCLUSIONMAPPING"}, // SHADERTYPE_PBR_PARALLAXOCCLUSIONMAPPING,
		//	{"ANISOTROPIC"}, // SHADERTYPE_PBR_ANISOTROPIC,
		//	{"WATER"}, // SHADERTYPE_WATER,
		//	{"CARTOON"}, // SHADERTYPE_CARTOON,
		//	{"UNLIT"}, // SHADERTYPE_UNLIT,
		//	{"SHEEN"}, // SHADERTYPE_PBR_CLOTH,
		//	{"CLEARCOAT"}, // SHADERTYPE_PBR_CLEARCOAT,
		//	{"SHEEN", "CLEARCOAT"}, // SHADERTYPE_PBR_CLOTH_CLEARCOAT,
		//	{"TERRAINBLENDED"}, //SHADERTYPE_PBR_TERRAINBLENDED
		//};
		static_assert(SHADERTYPE_BIN_COUNT == arraysize(shaderTypeDefines), "These values must match!");

		jobsystem::Dispatch(CTX_renderPS, SHADERTYPE_BIN_COUNT, 1, [](jobsystem::JobArgs args) {

			LoadShader(
				ShaderStage::PS,
				shaders[PSTYPE_RENDERABLE_PERMUTATION__BEGIN + args.jobIndex],
				"meshPS.cso",
				ShaderModel::SM_6_0,
				shaderTypeDefines[args.jobIndex] // permutation defines
			);

			});

		jobsystem::Dispatch(CTX_renderPS, SHADERTYPE_BIN_COUNT, 1, [](jobsystem::JobArgs args) {
		
			auto defines = shaderTypeDefines[args.jobIndex];
			defines.push_back("TRANSPARENT");
			LoadShader(
				ShaderStage::PS,
				shaders[PSTYPE_RENDERABLE_TRANSPARENT_PERMUTATION__BEGIN + args.jobIndex],
				"meshPS.cso",
				ShaderModel::SM_6_0,
				defines // permutation defines
			);
		
			});

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GS_PREPROCESS], "gs_preprocessCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GS_GAUSSIAN_OFFSET], "gs_offsetCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GS_DUPLICATED_GAUSSIANS], "gs_duplicateWithKeysCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GS_RADIX_HIST_GAUSSIANS], "gs_histCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GS_RADIX_SORT_GAUSSIANS], "gs_sortCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GS_IDENTIFY_TILE_RANGES], "gs_identifyTileRangesCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GS_RENDER_GAUSSIAN], "gs_renderCS.cso"); });

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_DVR_DEFAULT], "dvrCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_MESH_SLICER], "meshSlicerCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_MESH_CURVED_SLICER], "meshSlicerCS_curvedplane.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SLICER_OUTLINE], "slicerOutlineCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_SLICE_RESOLVE_KB2], "slicerResolveCS_KB2.cso"); });

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_MESHLET_PREPARE], "meshlet_prepareCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VIEW_RESOLVE], "view_resolveCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_VIEW_RESOLVE_MSAA], "view_resolveCS_MSAA.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LIGHTCULLING], "lightCullingCS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LIGHTCULLING_DEBUG], "lightCullingCS_DEBUG.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LIGHTCULLING_ADVANCED], "lightCullingCS_ADVANCED.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_LIGHTCULLING_ADVANCED_DEBUG], "lightCullingCS_ADVANCED_DEBUG.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAINCUBEARRAY_UNORM4], "generateMIPChainCubeArrayCS_unorm4.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAINCUBEARRAY_FLOAT4], "generateMIPChainCubeArrayCS_float4.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAINCUBE_UNORM4], "generateMIPChainCubeCS_unorm4.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAINCUBE_FLOAT4], "generateMIPChainCubeCS_float4.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAIN2D_FLOAT4], "generateMIPChain2DCS_float4.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAIN2D_UNORM4], "generateMIPChain2DCS_unorm4.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAIN3D_FLOAT4], "generateMIPChain3DCS_float4.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_GENERATEMIPCHAIN3D_UNORM4], "generateMIPChain3DCS_unorm4.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT1], "blur_gaussian_float1CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT3], "blur_gaussian_float3CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_FLOAT4], "blur_gaussian_float4CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_UNORM1], "blur_gaussian_unorm1CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_UNORM4], "blur_gaussian_unorm4CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT1], "blur_gaussian_wide_float1CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT3], "blur_gaussian_wide_float3CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_FLOAT4], "blur_gaussian_wide_float4CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_UNORM1], "blur_gaussian_wide_unorm1CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_BLUR_GAUSSIAN_WIDE_UNORM4], "blur_gaussian_wide_unorm4CS.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC1], "blockcompressCS_BC1.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC3], "blockcompressCS_BC3.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC4], "blockcompressCS_BC4.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC5], "blockcompressCS_BC5.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC6H], "blockcompressCS_BC6H.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_BLOCKCOMPRESS_BC6H_CUBEMAP], "blockcompressCS_BC6H_cubemap.cso"); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_WETMAP_UPDATE], "wetmap_updateCS.cso"); });

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { LoadShader(ShaderStage::CS, shaders[CSTYPE_POSTPROCESS_TONEMAP], "tonemapCS.cso"); });

		jobsystem::Wait(ctx);

		// create graphics pipelines
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_MESH_SIMPLE];
			desc.ps = &shaders[PSTYPE_MESH_SIMPLE];
			desc.rs = &rasterizers[RSTYPE_WIRE];
			desc.bs = &blendStates[BSTYPE_OPAQUE];
			desc.dss = &depthStencils[DSSTYPE_DEFAULT];

			device->CreatePipelineState(&desc, &PSO_wireframe);

			//desc.pt = PrimitiveTopology::PATCHLIST;
			//desc.vs = &common::shaders[VSTYPE_OBJECT_SIMPLE_TESSELLATION];
			//desc.hs = &common::shaders[HSTYPE_MESH_SIMPLE];
			//desc.ds = &common::shaders[DSTYPE_MESH_SIMPLE];
			//device->CreatePipelineState(&desc, &PSO_object_wire_tessellation);
			});

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) {
			PipelineStateDesc desc;
			desc.vs = &shaders[VSTYPE_OCCLUDEE];
			desc.rs = &rasterizers[RSTYPE_OCCLUDEE];
			desc.bs = &blendStates[BSTYPE_COLORWRITEDISABLE];
			desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
			desc.pt = PrimitiveTopology::TRIANGLESTRIP;

			device->CreatePipelineState(&desc, &PSO_occlusionquery);
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

		jobsystem::Dispatch(ctx, SHAPE_RENDERING_COUNT, 1, [](jobsystem::JobArgs args) {
			PipelineStateDesc desc;

			switch (args.jobIndex)
			{
			case SHAPE_RENDERING_LINES:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case SHAPE_RENDERING_LINES_DEPTH:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			}

			device->CreatePipelineState(&desc, &PSO_RenderableShapes[args.jobIndex]);
			});

		jobsystem::Wait(ctx);

		//for (uint32_t renderPass = 0; renderPass < RENDERPASS_COUNT; ++renderPass)
		for (uint32_t renderPass = 0; renderPass <= RENDERPASS_PREPASS_DEPTHONLY; ++renderPass)
		{
			const uint32_t mesh_shader = 0;
			//for (uint32_t mesh_shader = 0; mesh_shader <= (device->CheckCapability(GraphicsDeviceCapability::MESH_SHADER) ? 1u : 0u); ++mesh_shader)
			{
				// default objectshaders:
				//	We don't wait for these here, because then it can slow down the init time a lot
				//	We will wait for these to complete in RenderMeshes() just before they will be first used
				jobsystem::Wait(CTX_renderPSO[renderPass][mesh_shader]);
				CTX_renderPSO[renderPass][mesh_shader].priority = jobsystem::Priority::Low;
				for (uint32_t shaderType = 0; shaderType < SHADERTYPE_BIN_COUNT; ++shaderType)
				{
					jobsystem::Execute(CTX_renderPSO[renderPass][mesh_shader], [=](jobsystem::JobArgs args) {
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

										const uint32_t deferred_enabled = 0; // (TODO) 1

										SHADERTYPE realPS = GetPSTYPE((RENDERPASS)renderPass, deferred_enabled, alphatest, transparency, static_cast<MaterialComponent::ShaderType>(shaderType));
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
												if (renderPass == RENDERPASS_MAIN)
												{
													renderpass_info.rt_count = 1;
													renderpass_info.rt_formats[0] = FORMAT_rendertargetMain;
												}
												else 
												{
													renderpass_info.rt_count = 2;
													renderpass_info.rt_formats[0] = FORMAT_idbuffer;
													renderpass_info.rt_formats[1] = FORMAT_idbuffer;
												}
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