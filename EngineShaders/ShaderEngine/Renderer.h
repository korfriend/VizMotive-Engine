#pragma once
#include "GBackend/GBackendDevice.h"
#include "Components/GComponents.h"

#include "ShaderEngine.h"
#include "ShaderLoader.h"

#include "Utils/JobSystem.h"
#include "Utils/Spinlock.h"

#include "../Shaders/ShaderInterop.h"
#include "../Shaders/ShaderInterop_Postprocess.h"
#include "../Shaders/ShaderInterop_DVR.h"
#include "../Shaders/ShaderInterop_GaussianSplatting.h"
#include "../Shaders/ShaderInterop_BVH.h"
#include "../Shaders/ShaderInterop_FSR2.h"


using namespace vz::graphics;
using namespace vz::shader;

static_assert(SHADERTYPE_BIN_COUNT == SCU32(vz::MaterialComponent::ShaderType::COUNT));
static_assert(TEXTURESLOT_COUNT == SCU32(vz::MaterialComponent::TextureSlot::TEXTURESLOT_COUNT));
static_assert(VOLUME_TEXTURESLOT_COUNT == SCU32(vz::MaterialComponent::VolumeTextureSlot::VOLUME_TEXTURESLOT_COUNT));
static_assert(LOOKUPTABLE_COUNT == SCU32(vz::MaterialComponent::LookupTableSlot::LOOKUPTABLE_COUNT));

//RenderableComponent::RenderableFlags
static_assert(INST_CLIPBOX == SCU32(vz::RenderableComponent::RenderableFlags::CLIP_BOX));
static_assert(INST_CLIPPLANE == SCU32(vz::RenderableComponent::RenderableFlags::CLIP_PLANE));
static_assert(INST_JITTERING == SCU32(vz::RenderableComponent::RenderableFlags::JITTER_SAMPLE));

//----- global constants -----
namespace vz::renderer
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

	using BlendMode = MaterialComponent::BlendMode;

	// Common blendmodes used across multiple systems
	constexpr uint32_t BLENDMODE_OPAQUE = SCU32(BlendMode::BLENDMODE_OPAQUE);
	constexpr uint32_t BLENDMODE_ALPHA = SCU32(BlendMode::BLENDMODE_ALPHA);
	constexpr uint32_t BLENDMODE_PREMULTIPLIED = SCU32(BlendMode::BLENDMODE_PREMULTIPLIED);
	constexpr uint32_t BLENDMODE_ADDITIVE = SCU32(BlendMode::BLENDMODE_ADDITIVE);
	constexpr uint32_t BLENDMODE_MULTIPLY = SCU32(BlendMode::BLENDMODE_MULTIPLY);
	constexpr uint32_t BLENDMODE_COUNT = SCU32(BlendMode::BLENDMODE_COUNT);
}
	
//----- enumerations -----
namespace vz::renderer
{
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
		BUFFERTYPE_INDIRECT_DEBUG_0,
		BUFFERTYPE_INDIRECT_DEBUG_1,
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
		SAMPLER_LINEAR_BORDER,
		SAMPLER_POINT_CLAMP,
		SAMPLER_POINT_WRAP,
		SAMPLER_POINT_MIRROR,
		SAMPLER_POINT_BORDER,
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
}

//----- renderer interfaces -----
namespace vz::renderer
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

	bool IsInitialized();
	uint32_t ComputeObjectLODForView(const RenderableComponent& renderable, 
		const geometrics::AABB& aabb, const GeometryComponent& geometry, const XMMATRIX& ViewProjection);
}

// external parameters
namespace vz::renderer
{
	extern InputLayout			inputLayouts[ILTYPE_COUNT];
	extern RasterizerState		rasterizers[RSTYPE_COUNT];
	extern DepthStencilState	depthStencils[DSSTYPE_COUNT];
	extern BlendState			blendStates[BSTYPE_COUNT];
	extern Shader				shaders[SHADERTYPE_COUNT];
	extern GPUBuffer			buffers[BUFFERTYPE_COUNT];
	extern Sampler				samplers[SAMPLER_COUNT];
	extern Texture				textures[TEXTYPE_COUNT];

	extern GPUBuffer			indirectDebugStatsReadback[GraphicsDevice::GetBufferCount()];
	extern bool					indirectDebugStatsReadback_available[GraphicsDevice::GetBufferCount()];

	extern std::unordered_map<uint32_t, PipelineState> PSO_render[RENDERPASS_COUNT][SHADERTYPE_BIN_COUNT];

	PipelineState* GetObjectPSO(MeshRenderingVariant variant);

	extern jobsystem::context	CTX_renderPSO[RENDERPASS_COUNT][MESH_SHADER_PSO_COUNT];

	extern PipelineState		PSO_wireframe;
	extern PipelineState		PSO_occlusionquery;
	extern PipelineState		PSO_RenderableShapes[SHAPE_RENDERING_COUNT];
	extern PipelineState		PSO_lightvisualizer[SCU32(LightComponent::LightType::COUNT)];
	extern PipelineState		PSO_volumetriclight[SCU32(LightComponent::LightType::COUNT)];

	// progressive components
	extern std::vector<Entity> deferredGeometryGPUBVHGens; // BVHBuffers
	extern std::vector<std::pair<Texture, bool>> deferredMIPGens;
	extern std::vector<std::pair<Texture, Texture>> deferredBCQueue; // BC : Block Compression
	extern std::vector<std::pair<Texture, Texture>> deferredTextureCopy;
	extern std::vector<std::pair<GPUBuffer, std::pair<void*, size_t>>> deferredBufferUpdate;

	//extern SpinLock deferredResourceLock;
	extern std::mutex deferredResourceMutex;
}

// ----- Rendering System Options -----
namespace vz::renderer
{
	extern float giBoost;
	extern float renderingSpeed;
	extern bool isOcclusionCullingEnabled;
	extern bool isFreezeCullingCameraEnabled;
	extern bool isWetmapRefreshEnabled;
	extern bool isSceneUpdateEnabled;
	extern bool isTemporalAAEnabled;
	extern bool isTemporalAADebugEnabled;
	extern bool isDisableAlbedoMaps;
	extern bool isForceDiffuseLighting;
	extern bool isTessellationEnabled;
	extern bool isVolumeLightsEnabled;
	extern bool isLensFlareEnabled;
	extern bool isLightShaftsEnabled;
	extern bool isFSREnabled;
	extern bool isTonemapping;
	extern bool isWireRender;
	extern bool isDebugLightCulling;
	extern bool isAdvancedLightCulling;
	extern bool isMeshShaderAllowed;
	extern bool isShadowsEnabled;
	extern bool isShadowLODOverride;
	extern bool isVariableRateShadingClassification;
	extern bool isSurfelGIDebugEnabled;
	extern bool isColorGradingEnabled;
	extern bool isGaussianSplattingEnabled;

	extern bool isDebugShapeEnabled;
	extern bool isDebugShapeCleanStart;
}

#define ReleaseRenderRes(SRC, R_COUNT) for (size_t i = 0, n = (size_t)R_COUNT; i < n; ++i) SRC[i] = {};