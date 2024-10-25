#pragma once
#include "Common/Backend/GBackendDevice.h"
#include "Components/GComponents.h"

#include "PluginInterface.h"

#include "Utils/JobSystem.h"
#include "Shaders/ShaderInterop.h"

using namespace vz::graphics;

static const uint32_t SHADERTYPE_BIN_COUNT = (uint32_t)vz::MaterialComponent::ShaderType::COUNT;

//----- global constants -----
namespace vz
{
	// global resources //
	//	will be used across different combinations of scenes and cameras
	constexpr Format FORMAT_depthbufferMain = Format::D32_FLOAT_S8X24_UINT;
	constexpr Format FORMAT_rendertargetMain = Format::R11G11B10_FLOAT;
	constexpr Format FORMAT_idbuffer = Format::R32_UINT;
	constexpr Format FORMAT_rendertargetShadowmap = Format::R16G16B16A16_FLOAT;
	constexpr Format FORMAT_depthbufferShadowmap = Format::D16_UNORM;
	constexpr Format FORMAT_rendertargetEnvprobe = Format::R11G11B10_FLOAT;
	constexpr Format FORMAT_depthbufferEnvprobe = Format::D16_UNORM;

	// Common blendmodes used across multiple systems
	constexpr uint32_t BLENDMODE_OPAQUE = SCU32(MaterialComponent::BlendMode::BLENDMODE_OPAQUE);
	constexpr uint32_t BLENDMODE_ALPHA = SCU32(MaterialComponent::BlendMode::BLENDMODE_ALPHA);
	constexpr uint32_t BLENDMODE_PREMULTIPLIED = SCU32(MaterialComponent::BlendMode::BLENDMODE_PREMULTIPLIED);
	constexpr uint32_t BLENDMODE_ADDITIVE = SCU32(MaterialComponent::BlendMode::BLENDMODE_ADDITIVE);
	constexpr uint32_t BLENDMODE_MULTIPLY = SCU32(MaterialComponent::BlendMode::BLENDMODE_MULTIPLY);
	constexpr uint32_t BLENDMODE_COUNT = SCU32(MaterialComponent::BlendMode::BLENDMODE_COUNT);
}
	
//----- enumerations -----
namespace vz	
{
	// shaders
	enum SHADERTYPE : uint32_t
	{
		// NOTE :
		//	* currently (24.10.03) provides only VER 0.1 marked shader types
		//	* post-pix naming "EMULATION" refers to additional shader for unsupported shader feature

		////////////////////
		// vertex shaders //
		VSTYPE_MESH_DEBUG,		// VER 0.1
		VSTYPE_MESH_COMMON,		// VER 0.1
		VSTYPE_MESH_SIMPLE,		// VER 0.1
		VSTYPE_OCCLUDEE,		// VER 0.1
		VSTYPE_VERTEXCOLOR,

		VSTYPE_MESH_COMMON_TESSELLATION,

		VSTYPE_MESH_PREPASS,
		VSTYPE_MESH_PREPASS_ALPHATEST,
		VSTYPE_MESH_PREPASS_TESSELLATION,
		VSTYPE_MESH_PREPASS_ALPHATEST_TESSELLATION,

		VSTYPE_ENVMAP,

		VSTYPE_SHADOW_TRANSPARENT,
		VSTYPE_SHADOW_ALPHATEST,
		VSTYPE_SHADOW,

		VSTYPE_VOXELIZER,

		//////////////////////
		// geometry shaders //
		GSTYPE_VOXELIZER,
		GSTYPE_ENVMAP_EMULATION,
		GSTYPE_SHADOW_TRANSPARENT_EMULATION,
		GSTYPE_SHADOW_ALPHATEST_EMULATION,
		GSTYPE_SHADOW_EMULATION,

		//////////////////
		// hull shaders //
		HSTYPE_MESH,
		HSTYPE_MESH_PREPASS,
		HSTYPE_MESH_PREPASS_ALPHATEST,

		////////////////////
		// domain shaders //
		DSTYPE_MESH,
		DSTYPE_MESH_PREPASS,
		DSTYPE_MESH_PREPASS_ALPHATEST,

		///////////////////
		// pixel shaders //
		PSTYPE_DEBUG,	// VER 0.1 // debug output (to final render target)
		PSTYPE_SIMPLE,	// VER 0.1 // no shading (to final render target)
		PSTYPE_VERTEXCOLOR,

		PSTYPE_MESH_PREPASS,
		PSTYPE_MESH_PREPASS_ALPHATEST,
		PSTYPE_MESH_PREPASS_DEPTHONLY,
		PSTYPE_MESH_PREPASS_DEPTHONLY_ALPHATEST,

		PSTYPE_SHADOW_TRANSPARENT,
		PSTYPE_SHADOW_ALPHATEST,

		PSTYPE_ENVMAP,

		PSTYPE_VOXELIZER,

		PSTYPE_RENDERABLE_PERMUTATION__BEGIN, // VER 0.1
		PSTYPE_RENDERABLE_PERMUTATION__END = PSTYPE_RENDERABLE_PERMUTATION__BEGIN + SHADERTYPE_BIN_COUNT, // VER 0.1
		PSTYPE_RENDERABLE_TRANSPARENT_PERMUTATION__BEGIN,
		PSTYPE_RENDERABLE_TRANSPARENT_PERMUTATION__END = PSTYPE_RENDERABLE_TRANSPARENT_PERMUTATION__BEGIN + SHADERTYPE_BIN_COUNT,

		CSTYPE_VIEW_SURFACE_PERMUTATION__BEGIN,
		CSTYPE_VIEW_SURFACE_PERMUTATION__END = CSTYPE_VIEW_SURFACE_PERMUTATION__BEGIN + SHADERTYPE_BIN_COUNT,
		CSTYPE_VIEW_SURFACE_REDUCED_PERMUTATION__BEGIN,
		CSTYPE_VIEW_SURFACE_REDUCED_PERMUTATION__END = CSTYPE_VIEW_SURFACE_REDUCED_PERMUTATION__BEGIN + SHADERTYPE_BIN_COUNT,

		CSTYPE_VIEW_SHADE_PERMUTATION__BEGIN,
		CSTYPE_VIEW_SHADE_PERMUTATION__END = CSTYPE_VIEW_SHADE_PERMUTATION__BEGIN + SHADERTYPE_BIN_COUNT,

		CSTYPE_VIEW_RESOLVE, // VER 0.1
		CSTYPE_VIEW_RESOLVE_MSAA,
		CSTYPE_TILEFRUSTUMS, // VER 0.1
		CSTYPE_LIGHTCULLING_ADVANCED,
		CSTYPE_LIGHTCULLING_ADVANCED_DEBUG,
		CSTYPE_LIGHTCULLING_DEBUG,
		CSTYPE_LIGHTCULLING, // VER 0.1
		
		CSTYPE_WETMAP_UPDATE, // VER 0.1


		///////////////////
		// mesh shaders //
		ASTYPE_MESH, // (Future Work) for mesh shader
		MSTYPE_MESH, // (Future Work) for mesh shader
		MSTYPE_MESH_PREPASS_ALPHATEST, // (Future Work) for mesh shader
		MSTYPE_MESH_PREPASS, // (Future Work) for mesh shader
		MSTYPE_SHADOW_TRANSPARENT, // (Future Work) for mesh shader
		MSTYPE_SHADOW_ALPHATEST, // (Future Work) for mesh shader
		MSTYPE_SHADOW, // (Future Work) for mesh shader

		SHADERTYPE_COUNT,
	};

	enum MESH_SHADER_PSO
	{
		MESH_SHADER_PSO_DISABLED,
		MESH_SHADER_PSO_ENABLED,
		MESH_SHADER_PSO_COUNT
	};

	// textures
	enum TEXTYPES
	{
		//TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT,
		//TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT,
		//TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT,
		//TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT,
		//TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT,
		TEXTYPE_2D_SHEENLUT,
		//TEXTYPE_2D_CAUSTICS,
		TEXTYPE_COUNT
	};

	// input layouts
	enum ILTYPES
	{
		ILTYPE_VERTEXCOLOR,
		ILTYPE_POSITION,
		ILTYPE_COUNT
	};

	// rasterizer states
	enum RSTYPES
	{
		RSTYPE_FRONT,
		RSTYPE_BACK,
		RSTYPE_DOUBLESIDED,
		RSTYPE_WIRE,
		RSTYPE_WIRE_SMOOTH,
		RSTYPE_WIRE_DOUBLESIDED,
		RSTYPE_WIRE_DOUBLESIDED_SMOOTH,
		RSTYPE_SHADOW,
		RSTYPE_SHADOW_DOUBLESIDED,
		RSTYPE_OCCLUDEE,
		RSTYPE_VOXELIZE,
		RSTYPE_SKY,
		RSTYPE_COUNT
	};

	// depth-stencil states
	enum DSSTYPES
	{
		DSSTYPE_DEFAULT,
		DSSTYPE_TRANSPARENT,
		DSSTYPE_SHADOW,
		DSSTYPE_DEPTHDISABLED,
		DSSTYPE_DEPTHREAD,
		DSSTYPE_DEPTHREADEQUAL,
		DSSTYPE_ENVMAP,
		DSSTYPE_CAPTUREIMPOSTOR,
		DSSTYPE_WRITEONLY,
		DSSTYPE_HOLOGRAM,
		DSSTYPE_COPY_STENCIL_BIT_0,
		DSSTYPE_COPY_STENCIL_BIT_1,
		DSSTYPE_COPY_STENCIL_BIT_2,
		DSSTYPE_COPY_STENCIL_BIT_3,
		DSSTYPE_COPY_STENCIL_BIT_4,
		DSSTYPE_COPY_STENCIL_BIT_5,
		DSSTYPE_COPY_STENCIL_BIT_6,
		DSSTYPE_COPY_STENCIL_BIT_7,
		DSSTYPE_COPY_STENCIL_BIT_8,
		DSSTYPE_COUNT
	};

	enum BSTYPES
	{
		BSTYPE_OPAQUE,
		BSTYPE_TRANSPARENT,
		BSTYPE_ADDITIVE,
		BSTYPE_PREMULTIPLIED,
		BSTYPE_COLORWRITEDISABLE,
		BSTYPE_MULTIPLY,
		BSTYPE_TRANSPARENTSHADOW,
		BSTYPE_COUNT
	};

	// buffers
	enum BUFFERTYPES
	{
		BUFFERTYPE_FRAMECB,
		BUFFERTYPE_COUNT
	};

	enum SAMPLERTYPES
	{
		// Can be changed by user
		SAMPLER_OBJECTSHADER,

		// Persistent samplers
		// These are bound once and are alive forever
		SAMPLER_LINEAR_CLAMP,
		SAMPLER_LINEAR_WRAP,
		SAMPLER_LINEAR_MIRROR,
		SAMPLER_POINT_CLAMP,
		SAMPLER_POINT_WRAP,
		SAMPLER_POINT_MIRROR,
		SAMPLER_ANISO_CLAMP,
		SAMPLER_ANISO_WRAP,
		SAMPLER_ANISO_MIRROR,
		SAMPLER_CMP_DEPTH,

		SAMPLER_COUNT,
	};

	enum RENDERPASS
	{
		RENDERPASS_MAIN,
		RENDERPASS_PREPASS,
		RENDERPASS_PREPASS_DEPTHONLY,
		RENDERPASS_ENVMAPCAPTURE,
		RENDERPASS_SHADOW,
		RENDERPASS_VOXELIZE,
		RENDERPASS_COUNT
	};

	enum DEBUGRENDERING
	{
		DEBUGRENDERING_GRID,
		DEBUGRENDERING_CUBE,
		DEBUGRENDERING_CUBE_DEPTH,
		DEBUGRENDERING_LINES,
		DEBUGRENDERING_LINES_DEPTH,
		DEBUGRENDERING_TRIANGLE_SOLID,
		DEBUGRENDERING_TRIANGLE_WIREFRAME,
		DEBUGRENDERING_TRIANGLE_SOLID_DEPTH,
		DEBUGRENDERING_TRIANGLE_WIREFRAME_DEPTH,
		DEBUGRENDERING_EMITTER,
		DEBUGRENDERING_COUNT
	};
}

//----- renderer interfaces -----
namespace vz
{
	union MeshRenderingVariant
	{
		struct
		{
			uint32_t renderpass : 4;	// enums::RENDERPASS
			uint32_t shadertype : 8;	// MaterialComponent::ShaderType::COUNT
			uint32_t blendmode : 4;		// enums::BLENDMODE
			uint32_t cullmode : 2;		// graphics::CullMode
			uint32_t tessellation : 1;	// bool
			uint32_t alphatest : 1;		// bool
			uint32_t sample_count : 4;	// 1, 2, 4, 8
			uint32_t mesh_shader : 1;	// bool
		} bits;
		uint32_t value;
	};
	static_assert(sizeof(MeshRenderingVariant) == sizeof(uint32_t));

	namespace initializer
	{
		void SetUpStates();
		void LoadBuffers();
	}

	namespace shader
	{
		bool LoadShader(
			ShaderStage stage,
			Shader& shader,
			const std::string& filename,
			ShaderModel minshadermodel = graphics::ShaderModel::SM_6_0,
			const std::vector<std::string>& permutation_defines = {}
		);

		void LoadShaders();
		inline PipelineState* GetObjectPSO(MeshRenderingVariant variant);
	}

	namespace renderer
	{
		const Sampler* GetSampler(SAMPLERTYPES id);
	}

	struct GSceneDetails : GScene
	{
		GSceneDetails(Scene* scene) : GScene(scene) {}

		// note all GPU resources (their pointers) are managed by
		//  ComPtr or 
		//  RAII (Resource Acquisition Is Initialization) patterns

		// * This renderer plugin is based on Bindless Graphics 
		//	(https://developer.download.nvidia.com/opengl/tutorials/bindless_graphics.pdf)

		float deltaTime = 0.f;
		std::vector<Entity> renderableEntities; // cached (non enclosing for jobsystem)
		std::vector<Entity> lightEntities; // cached (non enclosing for jobsystem)
		std::vector<Entity> geometryEntities; // cached (non enclosing for jobsystem)
		std::vector<Entity> materialEntities; // cached (non enclosing for jobsystem)

		// Separate stream of world matrices:
		std::vector<XMFLOAT4X4> matrixRenderables;
		std::vector<XMFLOAT4X4> matrixRenderablesPrev;

		const bool occlusionQueryEnabled = false;
		const bool cameraFreezeCullingEnabled = false;
		bool isWetmapProcessingRequired = false;

		ShaderScene shaderscene = {};

		graphics::GraphicsDevice* device = nullptr;
		// Instances (parts) for bindless renderables:
		//	contains in order:
		//		1) renderables (normal meshes)
		size_t instanceArraySize = 0;
		graphics::GPUBuffer instanceUploadBuffer[graphics::GraphicsDevice::GetBufferCount()]; // dynamic GPU-usage
		graphics::GPUBuffer instanceBuffer = {};	// default GPU-usage
		ShaderMeshInstance* instanceArrayMapped = nullptr; // CPU-access buffer pointer for instanceUploadBuffer[%2]

		// Geometries for bindless visiblity indexing:
		//	contains in order:
		//		1) # of primitive parts
		//		2) emitted particles * 1
		graphics::GPUBuffer geometryUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		ShaderGeometry* geometryArrayMapped = nullptr;
		size_t geometryArraySize = 0;
		graphics::GPUBuffer geometryBuffer = {};
		std::atomic<uint32_t> geometryAllocator{ 0 };

		// Materials for bindless visibility indexing:
		size_t materialArraySize = 0;
		graphics::GPUBuffer materialUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		graphics::GPUBuffer materialBuffer = {};
		graphics::GPUBuffer textureStreamingFeedbackBuffer;	// a sinlge UINT
		graphics::GPUBuffer textureStreamingFeedbackBuffer_readback[graphics::GraphicsDevice::GetBufferCount()];
		const uint32_t* textureStreamingFeedbackMapped = nullptr;
		ShaderMaterial* materialArrayMapped = nullptr;

		// Occlusion query state:
		struct OcclusionResult
		{
			int occlusionQueries[graphics::GraphicsDevice::GetBufferCount()];
			// occlusion result history bitfield (32 bit->32 frame history)
			uint32_t occlusionHistory = ~0u;

			constexpr bool IsOccluded() const
			{
				// Perform a conservative occlusion test:
				// If it is visible in any frames in the history, it is determined visible in this frame
				// But if all queries failed in the history, it is occluded.
				// If it pops up for a frame after occluded, it is visible again for some frames
				return occlusionHistory == 0;
			}
		};
		mutable std::vector<OcclusionResult> occlusionResultsObjects;
		graphics::GPUQueryHeap queryHeap;
		graphics::GPUBuffer queryResultBuffer[graphics::GraphicsDevice::GetBufferCount()];
		graphics::GPUBuffer queryPredicationBuffer = {};
		uint32_t queryheapIdx = 0;
		mutable std::atomic<uint32_t> queryAllocator{ 0 };

		bool Update(const float dt) override;
		bool Destroy() override;

		void RunPrimtiveUpdateSystem(jobsystem::context& ctx);
		void RunMaterialUpdateSystem(jobsystem::context& ctx);
		void RunRenderableUpdateSystem(jobsystem::context& ctx); // note a single renderable can generate multiple mesh instances
	};
}