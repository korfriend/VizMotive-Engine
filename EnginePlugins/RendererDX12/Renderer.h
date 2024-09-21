#pragma once
#include "CommonInclude.h"
#include "Common/Backend/GBackendDevice.h"
#include "Components/Scene.h"
#include "ThirdParty/RectPacker.h"
#include "wiPrimitive.h"
#include "wiCanvas.h"
#include "Libs/Math.h"
#include "Shaders/ShaderInterop_Renderer.h"

#include <memory>
#include <limits>

// ... 
		// 1. legacy-style raster graphics via VS, GS, PS, ...
		// 2. mesh-shader... replace...

RenderPath2D::Update(dt);

float update_speed = 0;

scene->Update(update_speed);

// Frustum culling for main camera:
visibility_main.layerMask = getLayerMask();
visibility_main.scene = scene;
visibility_main.camera = camera;
visibility_main.flags = wi::renderer::Visibility::ALLOW_EVERYTHING;
if (!getOcclusionCullingEnabled())
{
	visibility_main.flags &= ~wi::renderer::Visibility::ALLOW_OCCLUSION_CULLING;
}
wi::renderer::UpdateVisibility(visibility_main);

if (visibility_main.planar_reflection_visible)
{
	// Frustum culling for planar reflections:
	camera_reflection = *camera;
	camera_reflection.jitter = XMFLOAT2(0, 0);
	camera_reflection.Reflect(visibility_main.reflectionPlane);
	visibility_reflection.layerMask = getLayerMask();
	visibility_reflection.scene = scene;
	visibility_reflection.camera = &camera_reflection;
	visibility_reflection.flags =
		wi::renderer::Visibility::ALLOW_OBJECTS |
		wi::renderer::Visibility::ALLOW_EMITTERS |
		wi::renderer::Visibility::ALLOW_HAIRS |
		wi::renderer::Visibility::ALLOW_LIGHTS
		;
	wi::renderer::UpdateVisibility(visibility_reflection);
}

XMUINT2 internalResolution = GetInternalResolution();

wi::renderer::UpdatePerFrameData(
	*scene,
	visibility_main,
	frameCB,
	getSceneUpdateEnabled() ? dt : 0
);

if (getFSR2Enabled())
{
	camera->jitter = fsr2Resources.GetJitter();
	camera_reflection.jitter.x = camera->jitter.x * 2;
	camera_reflection.jitter.y = camera->jitter.x * 2;
}
else if (wi::renderer::GetTemporalAAEnabled())
{
	const XMFLOAT4& halton = wi::math::GetHaltonSequence(wi::graphics::GetDevice()->GetFrameCount() % 256);
	camera->jitter.x = (halton.x * 2 - 1) / (float)internalResolution.x;
	camera->jitter.y = (halton.y * 2 - 1) / (float)internalResolution.y;
	camera_reflection.jitter.x = camera->jitter.x * 2;
	camera_reflection.jitter.y = camera->jitter.x * 2;
	if (!temporalAAResources.IsValid())
	{
		wi::renderer::CreateTemporalAAResources(temporalAAResources, internalResolution);
	}
}
else
{
	camera->jitter = XMFLOAT2(0, 0);
	camera_reflection.jitter = XMFLOAT2(0, 0);
	temporalAAResources = {};
}

camera->UpdateCamera();
if (visibility_main.planar_reflection_visible)
{
	camera_reflection.UpdateCamera();
}

if (getAO() != AO_RTAO)
{
	rtaoResources.frame = 0;
}
if (!wi::renderer::GetRaytracedShadowsEnabled())
{
	rtshadowResources.frame = 0;
}
if (!getSSREnabled() && !getRaytracedReflectionEnabled())
{
	rtSSR = {};
}
if (!getSSGIEnabled())
{
	rtSSGI = {};
}
if (!getRaytracedDiffuseEnabled())
{
	rtRaytracedDiffuse = {};
}
if (getAO() == AO_DISABLED)
{
	rtAO = {};
}

if (wi::renderer::GetRaytracedShadowsEnabled() && device->CheckCapability(GraphicsDeviceCapability::RAYTRACING))
{
	if (!rtshadowResources.denoised.IsValid())
	{
		wi::renderer::CreateRTShadowResources(rtshadowResources, internalResolution);
	}
}
else
{
	rtshadowResources = {};
}

if (scene->weather.IsRealisticSky() && scene->weather.IsRealisticSkyAerialPerspective())
{
	if (!aerialperspectiveResources.texture_output.IsValid())
	{
		wi::renderer::CreateAerialPerspectiveResources(aerialperspectiveResources, internalResolution);
	}
	if (getReflectionsEnabled() && depthBuffer_Reflection.IsValid())
	{
		if (!aerialperspectiveResources_reflection.texture_output.IsValid())
		{
			wi::renderer::CreateAerialPerspectiveResources(aerialperspectiveResources_reflection, XMUINT2(depthBuffer_Reflection.desc.width, depthBuffer_Reflection.desc.height));
		}
	}
	else
	{
		aerialperspectiveResources_reflection = {};
	}
}
else
{
	aerialperspectiveResources = {};
}

if (scene->weather.IsVolumetricClouds())
{
	if (!volumetriccloudResources.texture_cloudRender.IsValid())
	{
		wi::renderer::CreateVolumetricCloudResources(volumetriccloudResources, internalResolution);
	}
	if (getReflectionsEnabled() && depthBuffer_Reflection.IsValid())
	{
		if (!volumetriccloudResources_reflection.texture_cloudRender.IsValid())
		{
			wi::renderer::CreateVolumetricCloudResources(volumetriccloudResources_reflection, XMUINT2(depthBuffer_Reflection.desc.width, depthBuffer_Reflection.desc.height));
		}
	}
	else
	{
		volumetriccloudResources_reflection = {};
	}
}
else
{
	volumetriccloudResources = {};
}

if (!scene->waterRipples.empty())
{
	if (!rtWaterRipple.IsValid())
	{
		TextureDesc desc;
		desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
		desc.format = Format::R16G16_FLOAT;
		desc.width = internalResolution.x / 8;
		desc.height = internalResolution.y / 8;
		assert(ComputeTextureMemorySizeInBytes(desc) <= ComputeTextureMemorySizeInBytes(rtParticleDistortion.desc)); // aliasing check
		device->CreateTexture(&desc, nullptr, &rtWaterRipple, &rtParticleDistortion); // aliased!
		device->SetName(&rtWaterRipple, "rtWaterRipple");
	}
}
else
{
	rtWaterRipple = {};
}

if (wi::renderer::GetSurfelGIEnabled())
{
	if (!surfelGIResources.result.IsValid())
	{
		wi::renderer::CreateSurfelGIResources(surfelGIResources, internalResolution);
	}
}

if (wi::renderer::GetVXGIEnabled())
{
	if (!vxgiResources.IsValid())
	{
		wi::renderer::CreateVXGIResources(vxgiResources, internalResolution);
	}
}
else
{
	vxgiResources = {};
}

// Check whether reprojected depth is required:
if (!first_frame && wi::renderer::IsMeshShaderAllowed() && wi::renderer::IsMeshletOcclusionCullingEnabled())
{
	TextureDesc desc;
	desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
	desc.format = Format::R16_UNORM;
	desc.width = internalResolution.x;
	desc.height = internalResolution.y;
	desc.mip_levels = GetMipCount(desc.width, desc.height, 1, 4);
	desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
	device->CreateTexture(&desc, nullptr, &reprojectedDepth);
	device->SetName(&reprojectedDepth, "reprojectedDepth");

	for (uint32_t i = 0; i < reprojectedDepth.desc.mip_levels; ++i)
	{
		int subresource_index;
		subresource_index = device->CreateSubresource(&reprojectedDepth, SubresourceType::SRV, 0, 1, i, 1);
		assert(subresource_index == i);
		subresource_index = device->CreateSubresource(&reprojectedDepth, SubresourceType::UAV, 0, 1, i, 1);
		assert(subresource_index == i);
	}
}
else
{
	reprojectedDepth = {};
}

// Check whether velocity buffer is required:
if (
	getMotionBlurEnabled() ||
	wi::renderer::GetTemporalAAEnabled() ||
	getSSREnabled() ||
	getSSGIEnabled() ||
	getRaytracedReflectionEnabled() ||
	getRaytracedDiffuseEnabled() ||
	wi::renderer::GetRaytracedShadowsEnabled() ||
	getAO() == AO::AO_RTAO ||
	wi::renderer::GetVariableRateShadingClassification() ||
	getFSR2Enabled() ||
	reprojectedDepth.IsValid()
	)
{
	if (!rtVelocity.IsValid())
	{
		TextureDesc desc;
		desc.format = Format::R16G16_FLOAT;
		desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS | BindFlag::RENDER_TARGET;
		desc.width = internalResolution.x;
		desc.height = internalResolution.y;
		desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
		device->CreateTexture(&desc, nullptr, &rtVelocity);
		device->SetName(&rtVelocity, "rtVelocity");
	}
}
else
{
	rtVelocity = {};
}

// Check whether shadow mask is required:
if (wi::renderer::GetScreenSpaceShadowsEnabled() || wi::renderer::GetRaytracedShadowsEnabled())
{
	if (!rtShadow.IsValid())
	{
		TextureDesc desc;
		desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
		desc.format = Format::R8_UNORM;
		desc.array_size = 16;
		desc.width = internalResolution.x;
		desc.height = internalResolution.y;
		desc.layout = ResourceState::SHADER_RESOURCE_COMPUTE;
		device->CreateTexture(&desc, nullptr, &rtShadow);
		device->SetName(&rtShadow, "rtShadow");
	}
}
else
{
	rtShadow = {};
}

if (getFSR2Enabled())
{
	// FSR2 also acts as a temporal AA, so we inform the shaders about it here
	//	This will allow improved stochastic alpha test transparency
	frameCB.options |= OPTION_BIT_TEMPORALAA_ENABLED;
	uint x = frameCB.frame_count % 4;
	uint y = frameCB.frame_count / 4;
	frameCB.temporalaa_samplerotation = (x & 0x000000FF) | ((y & 0x000000FF) << 8);
}

// Check whether visibility resources are required:
if (
	visibility_shading_in_compute ||
	getSSREnabled() ||
	getSSGIEnabled() ||
	getRaytracedReflectionEnabled() ||
	getRaytracedDiffuseEnabled() ||
	wi::renderer::GetScreenSpaceShadowsEnabled() ||
	wi::renderer::GetRaytracedShadowsEnabled() ||
	wi::renderer::GetVXGIEnabled()
	)
{
	if (!visibilityResources.IsValid())
	{
		wi::renderer::CreateVisibilityResources(visibilityResources, internalResolution);
	}
}
else
{
	visibilityResources = {};
}

// Check for depth of field allocation:
if (getDepthOfFieldEnabled() &&
	getDepthOfFieldStrength() > 0 &&
	camera->aperture_size > 0
	)
{
	if (!depthoffieldResources.IsValid())
	{
		XMUINT2 resolution = GetInternalResolution();
		if (getFSR2Enabled())
		{
			resolution = XMUINT2(GetPhysicalWidth(), GetPhysicalHeight());
		}
		wi::renderer::CreateDepthOfFieldResources(depthoffieldResources, resolution);
	}
}
else
{
	depthoffieldResources = {};
}

// Check for motion blur allocation:
if (getMotionBlurEnabled() && getMotionBlurStrength() > 0)
{
	if (!motionblurResources.IsValid())
	{
		XMUINT2 resolution = GetInternalResolution();
		if (getFSR2Enabled())
		{
			resolution = XMUINT2(GetPhysicalWidth(), GetPhysicalHeight());
		}
		wi::renderer::CreateMotionBlurResources(motionblurResources, resolution);
	}
}
else
{
	motionblurResources = {};
}

// Keep a copy of last frame's depth buffer for temporal disocclusion checks, so swap with current one every frame:
std::swap(depthBuffer_Copy, depthBuffer_Copy1);

visibilityResources.depthbuffer = &depthBuffer_Copy;
visibilityResources.lineardepth = &rtLinearDepth;
if (getMSAASampleCount() > 1)
{
	visibilityResources.primitiveID_resolved = &rtPrimitiveID;
}
else
{
	visibilityResources.primitiveID_resolved = nullptr;
}

camera->canvas.init(*this);
camera->width = (float)internalResolution.x;
camera->height = (float)internalResolution.y;
camera->scissor = GetScissorInternalResolution();
camera->sample_count = depthBuffer_Main.desc.sample_count;
camera->shadercamera_options = SHADERCAMERA_OPTION_NONE;
camera->texture_primitiveID_index = device->GetDescriptorIndex(&rtPrimitiveID, SubresourceType::SRV);
camera->texture_depth_index = device->GetDescriptorIndex(&depthBuffer_Copy, SubresourceType::SRV);
camera->texture_lineardepth_index = device->GetDescriptorIndex(&rtLinearDepth, SubresourceType::SRV);
camera->texture_velocity_index = device->GetDescriptorIndex(&rtVelocity, SubresourceType::SRV);
camera->texture_normal_index = device->GetDescriptorIndex(&visibilityResources.texture_normals, SubresourceType::SRV);
camera->texture_roughness_index = device->GetDescriptorIndex(&visibilityResources.texture_roughness, SubresourceType::SRV);
camera->buffer_entitytiles_index = device->GetDescriptorIndex(&tiledLightResources.entityTiles, SubresourceType::SRV);
camera->texture_reflection_index = device->GetDescriptorIndex(&rtReflection, SubresourceType::SRV);
camera->texture_reflection_depth_index = device->GetDescriptorIndex(&depthBuffer_Reflection, SubresourceType::SRV);
camera->texture_refraction_index = device->GetDescriptorIndex(&rtSceneCopy, SubresourceType::SRV);
camera->texture_waterriples_index = device->GetDescriptorIndex(&rtWaterRipple, SubresourceType::SRV);
camera->texture_ao_index = device->GetDescriptorIndex(&rtAO, SubresourceType::SRV);
camera->texture_ssr_index = device->GetDescriptorIndex(&rtSSR, SubresourceType::SRV);
camera->texture_ssgi_index = device->GetDescriptorIndex(&rtSSGI, SubresourceType::SRV);
if (rtShadow.IsValid())
{
	camera->shadercamera_options |= SHADERCAMERA_OPTION_USE_SHADOW_MASK;
	camera->texture_rtshadow_index = device->GetDescriptorIndex(&rtShadow, SubresourceType::SRV);
}
else
{
	camera->texture_rtshadow_index = device->GetDescriptorIndex(wi::texturehelper::getWhite(), SubresourceType::SRV); // AMD descriptor branching fix
}
camera->texture_rtdiffuse_index = device->GetDescriptorIndex(&rtRaytracedDiffuse, SubresourceType::SRV);
camera->texture_surfelgi_index = device->GetDescriptorIndex(&surfelGIResources.result, SubresourceType::SRV);
camera->texture_vxgi_diffuse_index = device->GetDescriptorIndex(&vxgiResources.diffuse, SubresourceType::SRV);
if (wi::renderer::GetVXGIReflectionsEnabled())
{
	camera->texture_vxgi_specular_index = device->GetDescriptorIndex(&vxgiResources.specular, SubresourceType::SRV);
}
else
{
	camera->texture_vxgi_specular_index = -1;
}
camera->texture_reprojected_depth_index = device->GetDescriptorIndex(&reprojectedDepth, SubresourceType::SRV);

camera_reflection.canvas.init(*this);
camera_reflection.width = (float)depthBuffer_Reflection.desc.width;
camera_reflection.height = (float)depthBuffer_Reflection.desc.height;
camera_reflection.scissor.left = 0;
camera_reflection.scissor.top = 0;
camera_reflection.scissor.right = (int)depthBuffer_Reflection.desc.width;
camera_reflection.scissor.bottom = (int)depthBuffer_Reflection.desc.height;
camera_reflection.sample_count = depthBuffer_Reflection.desc.sample_count;
camera_reflection.shadercamera_options = SHADERCAMERA_OPTION_NONE;
camera_reflection.texture_primitiveID_index = -1;
camera_reflection.texture_depth_index = device->GetDescriptorIndex(&depthBuffer_Reflection, SubresourceType::SRV);
camera_reflection.texture_lineardepth_index = -1;
camera_reflection.texture_velocity_index = -1;
camera_reflection.texture_normal_index = -1;
camera_reflection.texture_roughness_index = -1;
camera_reflection.buffer_entitytiles_index = device->GetDescriptorIndex(&tiledLightResources_planarReflection.entityTiles, SubresourceType::SRV);
camera_reflection.texture_reflection_index = -1;
camera_reflection.texture_reflection_depth_index = -1;
camera_reflection.texture_refraction_index = -1;
camera_reflection.texture_waterriples_index = -1;
camera_reflection.texture_ao_index = -1;
camera_reflection.texture_ssr_index = -1;
camera_reflection.texture_ssgi_index = -1;
camera_reflection.texture_rtshadow_index = device->GetDescriptorIndex(wi::texturehelper::getWhite(), SubresourceType::SRV); // AMD descriptor branching fix
camera_reflection.texture_rtdiffuse_index = -1;
camera_reflection.texture_surfelgi_index = -1;
camera_reflection.texture_vxgi_diffuse_index = -1;
camera_reflection.texture_vxgi_specular_index = -1;
camera_reflection.texture_reprojected_depth_index = -1;

video_cmd = {};
if (getSceneUpdateEnabled() && scene->videos.GetCount() > 0)
{
	for (size_t i = 0; i < scene->videos.GetCount(); ++i)
	{
		const wi::scene::VideoComponent& video = scene->videos[i];
		if (wi::video::IsDecodingRequired(&video.videoinstance, dt))
		{
			video_cmd = device->BeginCommandList(QUEUE_VIDEO_DECODE);
			break;
		}
	}
	for (size_t i = 0; i < scene->videos.GetCount(); ++i)
	{
		wi::scene::VideoComponent& video = scene->videos[i];
		wi::video::UpdateVideo(&video.videoinstance, dt, video_cmd);
	}
}

namespace vz::enums {
	// engine stencil reference values. These can be in range of [0, 15].
	// Do not alter order or value because it is bound to lua manually!
	enum STENCILREF
	{
		STENCILREF_EMPTY = 0,
		STENCILREF_DEFAULT = 1,
		STENCILREF_CUSTOMSHADER = 2,
		STENCILREF_OUTLINE = 3,
		STENCILREF_CUSTOMSHADER_OUTLINE = 4,
		STENCILREF_LAST = 15
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

	// input layouts
	enum ILTYPES
	{
		ILTYPE_VERTEXCOLOR,
		ILTYPE_POSITION,
		ILTYPE_COUNT
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

	// blend states
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

	// textures
	enum TEXTYPES
	{
		TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW,
		TEXTYPE_2D_VOLUMETRICCLOUDS_SHADOW_FILTERED,
		TEXTYPE_2D_SKYATMOSPHERE_TRANSMITTANCELUT,
		TEXTYPE_2D_SKYATMOSPHERE_MULTISCATTEREDLUMINANCELUT,
		TEXTYPE_2D_SKYATMOSPHERE_SKYVIEWLUT,
		TEXTYPE_2D_SKYATMOSPHERE_SKYLUMINANCELUT,
		TEXTYPE_3D_SKYATMOSPHERE_CAMERAVOLUMELUT,
		TEXTYPE_2D_SHEENLUT,
		TEXTYPE_3D_WIND,
		TEXTYPE_3D_WIND_PREV,
		TEXTYPE_2D_CAUSTICS,
		TEXTYPE_COUNT
	};

	// shaders
	enum SHADERTYPE
	{
		// vertex shaders
		VSTYPE_OBJECT_DEBUG,
		VSTYPE_OBJECT_COMMON,
		VSTYPE_OBJECT_SIMPLE,

		VSTYPE_VERTEXCOLOR,

		// pixel shaders
		PSTYPE_OBJECT_SIMPLE,
		PSTYPE_VERTEXCOLOR,

		// geometry shaders
		
		// compute shaders

		// raytracing pipelines:


		SHADERTYPE_COUNT,
	};
}

namespace vz::renderer
{

	constexpr vz::graphics::Format format_depthbuffer_main = vz::graphics::Format::D32_FLOAT_S8X24_UINT;
	constexpr vz::graphics::Format format_rendertarget_main = vz::graphics::Format::R11G11B10_FLOAT;
	constexpr vz::graphics::Format format_idbuffer = vz::graphics::Format::R32_UINT;
	constexpr vz::graphics::Format format_rendertarget_shadowmap = vz::graphics::Format::R16G16B16A16_FLOAT;
	constexpr vz::graphics::Format format_depthbuffer_shadowmap = vz::graphics::Format::D16_UNORM;
	constexpr vz::graphics::Format format_rendertarget_envprobe = vz::graphics::Format::R11G11B10_FLOAT;
	constexpr vz::graphics::Format format_depthbuffer_envprobe = vz::graphics::Format::D16_UNORM;

	constexpr uint8_t raytracing_inclusion_mask_shadow = 1 << 0;
	constexpr uint8_t raytracing_inclusion_mask_reflection = 1 << 1;

	constexpr uint32_t CombineStencilrefs(vz::enums::STENCILREF engineStencilRef, uint8_t userStencilRef)
	{
		return (userStencilRef << 4) | static_cast<uint8_t>(engineStencilRef);
	}
	constexpr XMUINT2 GetEntityCullingTileCount(XMUINT2 internalResolution)
	{
		return XMUINT2(
			(internalResolution.x + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE,
			(internalResolution.y + TILED_CULLING_BLOCKSIZE - 1) / TILED_CULLING_BLOCKSIZE
		);
	}
	constexpr XMUINT2 GetVisibilityTileCount(XMUINT2 internalResolution)
	{
		return XMUINT2(
			(internalResolution.x + VISIBILITY_BLOCKSIZE - 1) / VISIBILITY_BLOCKSIZE,
			(internalResolution.y + VISIBILITY_BLOCKSIZE - 1) / VISIBILITY_BLOCKSIZE
		);
	}

	const vz::graphics::Sampler* GetSampler(vz::enums::SAMPLERTYPES id);
	const vz::graphics::Shader* GetShader(vz::enums::SHADERTYPE id);
	const vz::graphics::InputLayout* GetInputLayout(vz::enums::ILTYPES id);
	const vz::graphics::RasterizerState* GetRasterizerState(vz::enums::RSTYPES id);
	const vz::graphics::DepthStencilState* GetDepthStencilState(vz::enums::DSSTYPES id);
	const vz::graphics::BlendState* GetBlendState(vz::enums::BSTYPES id);
	const vz::graphics::GPUBuffer* GetBuffer(vz::enums::BUFFERTYPES id);
	const vz::graphics::Texture* GetTexture(vz::enums::TEXTYPES id);

	// Returns a buffer preinitialized for quad index buffer laid out as:
	//	vertexID * 4 + [0, 1, 2, 2, 1, 3]
	const vz::graphics::GPUBuffer& GetIndexBufferForQuads(uint32_t max_quad_count);

	void ModifyObjectSampler(const vz::graphics::SamplerDesc& desc);

	// Initializes the renderer
	void Initialize();

	// Returns the shader binary directory
	const std::string& GetShaderPath();
	// Sets the shader binary directory
	void SetShaderPath(const std::string& path);
	// Returns the shader source directory
	const std::string& GetShaderSourcePath();
	// Sets the shader source directory
	void SetShaderSourcePath(const std::string& path);
	// Reload shaders
	void ReloadShaders();

	bool LoadShader(
		vz::graphics::ShaderStage stage,
		vz::graphics::Shader& shader,
		const std::string& filename,
		vz::graphics::ShaderModel minshadermodel = vz::graphics::ShaderModel::SM_6_0,
		const std::vector<std::string>& permutation_defines = {}
	);

	// Whether background pipeline compilations are active
	bool IsPipelineCreationActive();

	struct Visibility
	{
		// User fills these:
		uint32_t layerMask = ~0u;
		const vz::scene::Scene* scene = nullptr;
		const vz::scene::CameraComponent* camera = nullptr;
		enum FLAGS
		{
			EMPTY = 0,
			ALLOW_OBJECTS = 1 << 0,
			ALLOW_LIGHTS = 1 << 1,
			ALLOW_DECALS = 1 << 2,
			ALLOW_ENVPROBES = 1 << 3,
			ALLOW_EMITTERS = 1 << 4,
			ALLOW_HAIRS = 1 << 5,
			ALLOW_REQUEST_REFLECTION = 1 << 6,
			ALLOW_OCCLUSION_CULLING = 1 << 7,
			ALLOW_SHADOW_ATLAS_PACKING = 1 << 8,

			ALLOW_EVERYTHING = ~0u
		};
		uint32_t flags = EMPTY;

		// vz::renderer::UpdateVisibility() fills these:
		vz::primitive::Frustum frustum;
		vz::vector<uint32_t> visibleObjects;
		vz::vector<uint32_t> visibleDecals;
		vz::vector<uint32_t> visibleEnvProbes;
		vz::vector<uint32_t> visibleEmitters;
		vz::vector<uint32_t> visibleHairs;
		vz::vector<uint32_t> visibleLights;
		vz::rectpacker::State shadow_packer;
		vz::rectpacker::Rect rain_blocker_shadow_rect;
		vz::vector<vz::rectpacker::Rect> visibleLightShadowRects;

		std::atomic<uint32_t> object_counter;
		std::atomic<uint32_t> light_counter;

		vz::SpinLock locker;
		bool planar_reflection_visible = false;
		float closestRefPlane = std::numeric_limits<float>::max();
		XMFLOAT4 reflectionPlane = XMFLOAT4(0, 1, 0, 0);
		std::atomic_bool volumetriclight_request{ false };

		void Clear()
		{
			visibleObjects.clear();
			visibleLights.clear();
			visibleDecals.clear();
			visibleEnvProbes.clear();
			visibleEmitters.clear();
			visibleHairs.clear();

			object_counter.store(0);
			light_counter.store(0);

			closestRefPlane = std::numeric_limits<float>::max();
			planar_reflection_visible = false;
			volumetriclight_request.store(false);
		}

		bool IsRequestedPlanarReflections() const
		{
			return planar_reflection_visible;
		}
		bool IsRequestedVolumetricLights() const
		{
			return volumetriclight_request.load();
		}
	};

	// Performs frustum culling.
	void UpdateVisibility(Visibility& vis);
	// Prepares the scene for rendering
	void UpdatePerFrameData(
		vz::scene::Scene& scene,
		const Visibility& vis,
		FrameCB& frameCB,
		float dt
	);
	// Updates the GPU state according to the previously called UpdatePerFrameData()
	void UpdateRenderData(
		const Visibility& vis,
		const FrameCB& frameCB,
		vz::graphics::CommandList cmd
	);

	// Updates those GPU states that can be async
	void UpdateRenderDataAsync(
		const Visibility& vis,
		const FrameCB& frameCB,
		vz::graphics::CommandList cmd
	);

	// Copies the texture streaming requests from GPU to CPU
	void TextureStreamingReadbackCopy(
		const vz::scene::Scene& scene,
		vz::graphics::CommandList cmd
	);

	// Binds all common constant buffers and samplers that may be used in all shaders
	void BindCommonResources(vz::graphics::CommandList cmd);
	// Updates the per camera constant buffer need to call for each different camera that is used when calling DrawScene() and the like
	//	camera_previous : camera from previous frame, used for reprojection effects.
	//	camera_reflection : camera that renders planar reflection
	void BindCameraCB(
		const vz::scene::CameraComponent& camera,
		const vz::scene::CameraComponent& camera_previous,
		const vz::scene::CameraComponent& camera_reflection,
		vz::graphics::CommandList cmd
	);


	enum DRAWSCENE_FLAGS
	{
		DRAWSCENE_OPAQUE = 1 << 0, // include opaque objects
		DRAWSCENE_TRANSPARENT = 1 << 1, // include transparent objects
		DRAWSCENE_OCCLUSIONCULLING = 1 << 2, // enable skipping objects based on occlusion culling results
		DRAWSCENE_TESSELLATION = 1 << 3, // enable tessellation
		DRAWSCENE_HAIRPARTICLE = 1 << 4, // include hair particles
		DRAWSCENE_IMPOSTOR = 1 << 5, // include impostors
		DRAWSCENE_OCEAN = 1 << 6, // include ocean
		DRAWSCENE_SKIP_PLANAR_REFLECTION_OBJECTS = 1 << 7, // don't draw subsets which have planar reflection material
		DRAWSCENE_FOREGROUND_ONLY = 1 << 8, // only include objects that are tagged as foreground
		DRAWSCENE_MAINCAMERA = 1 << 9, // If this is active, then ObjectComponent with SetNotVisibleInMainCamera(true) won't be drawn
	};

	// Draw the world from a camera. You must call BindCameraCB() at least once in this frame prior to this
	void DrawScene(
		const Visibility& vis,
		vz::enums::RENDERPASS renderPass,
		vz::graphics::CommandList cmd,
		uint32_t flags = DRAWSCENE_OPAQUE
	);

	// Process deferred requests such as AddDeferredMIPGen and AddDeferredBlockCompression:
	void ProcessDeferredTextureRequests(vz::graphics::CommandList cmd);

	// Draw shadow maps for each visible light that has associated shadow maps
	void DrawShadowmaps(
		const Visibility& vis,
		vz::graphics::CommandList cmd
	);
	// Draw debug world. You must also enable what parts to draw, eg. SetToDrawGridHelper, etc, see implementation for details what can be enabled.
	void DrawDebugWorld(
		const vz::scene::Scene& scene,
		const vz::scene::CameraComponent& camera,
		const vz::Canvas& canvas,
		vz::graphics::CommandList cmd
	);
	
	// Call once per frame to render lightmaps
	void RefreshLightmaps(const vz::scene::Scene& scene, vz::graphics::CommandList cmd);
	// Call once per frame to render wetmaps
	void RefreshWetmaps(const Visibility& vis, vz::graphics::CommandList cmd);
	// Run a compute shader that will resolve a MSAA depth buffer to a single-sample texture
	void ResolveMSAADepthBuffer(const vz::graphics::Texture& dst, const vz::graphics::Texture& src, vz::graphics::CommandList cmd);
	void DownsampleDepthBuffer(const vz::graphics::Texture& src, vz::graphics::CommandList cmd);

	struct VisibilityResources
	{
		XMUINT2 tile_count = {};
		vz::graphics::GPUBuffer bins;
		vz::graphics::GPUBuffer binned_tiles;
		vz::graphics::Texture texture_payload_0;
		vz::graphics::Texture texture_payload_1;
		vz::graphics::Texture texture_normals;
		vz::graphics::Texture texture_roughness;

		// You can request any of these extra outputs to be written by VisibilityResolve:
		const vz::graphics::Texture* depthbuffer = nullptr; // depth buffer that matches with post projection
		const vz::graphics::Texture* lineardepth = nullptr; // depth buffer in linear space in [0,1] range
		const vz::graphics::Texture* primitiveID_resolved = nullptr; // resolved from MSAA texture_visibility input

		inline bool IsValid() const { return bins.IsValid(); }
	};
	void CreateVisibilityResourcesLightWeight(VisibilityResources& res, XMUINT2 resolution);
	void CreateVisibilityResources(VisibilityResources& res, XMUINT2 resolution);
	void Visibility_Prepare(
		const VisibilityResources& res,
		const vz::graphics::Texture& input_primitiveID, // can be MSAA
		vz::graphics::CommandList cmd
	);
	void Visibility_Surface(
		const VisibilityResources& res,
		const vz::graphics::Texture& output,
		vz::graphics::CommandList cmd
	);
	void Visibility_Surface_Reduced(
		const VisibilityResources& res,
		vz::graphics::CommandList cmd
	);
	void Visibility_Shade(
		const VisibilityResources& res,
		const vz::graphics::Texture& output,
		vz::graphics::CommandList cmd
	);
	void Visibility_Velocity(
		const vz::graphics::Texture& output,
		vz::graphics::CommandList cmd
	);
	
	void Postprocess_Custom(
		const vz::graphics::Shader& computeshader,
		const vz::graphics::Texture& input,
		const vz::graphics::Texture& output,
		vz::graphics::CommandList cmd,
		const XMFLOAT4& params0 = XMFLOAT4(0, 0, 0, 0),
		const XMFLOAT4& params1 = XMFLOAT4(0, 0, 0, 0),
		const char* debug_name = "Postprocess_Custom"
	);

	void YUV_to_RGB(
		const vz::graphics::Texture& input,
		int input_subresource_luminance,
		int input_subresource_chrominance,
		const vz::graphics::Texture& output,
		vz::graphics::CommandList cmd
	);

	// This performs copies from separate depth and stencil shader resource textures
	//	into hardware depthStencil buffer that supports depth/stencil testing
	//	This is only supported by QUEUE_GRAPHICS!
	//	stencil_bits_to_copy : bitmask can be specified to mask out stencil bits that will be copied
	//	depthstencil_already_cleared : if false, it will be fully cleared if required; if true, it will be left intact
	void CopyDepthStencil(
		const vz::graphics::Texture* input_depth,
		const vz::graphics::Texture* input_stencil,
		const vz::graphics::Texture& output_depth_stencil,
		vz::graphics::CommandList cmd,
		uint8_t stencil_bits_to_copy = 0xFF,
		bool depthstencil_already_cleared = false
	);

	enum MIPGENFILTER
	{
		MIPGENFILTER_POINT,
		MIPGENFILTER_LINEAR,
		MIPGENFILTER_GAUSSIAN,
	};
	struct MIPGEN_OPTIONS
	{
		int arrayIndex = -1;
		const vz::graphics::Texture* gaussian_temp = nullptr;
		bool preserve_coverage = false;
		bool wide_gauss = false;
	};
	void GenerateMipChain(const vz::graphics::Texture& texture, MIPGENFILTER filter, vz::graphics::CommandList cmd, const MIPGEN_OPTIONS& options = {});

	// Compress a texture into Block Compressed format
	//	texture_src	: source uncompressed texture
	//	texture_bc	: destination comporessed texture, must be a supported BC format (BC1/BC3/BC4/BC5/BC6H_UFLOAT)
	//	Currently this will handle simple Texture2D with mip levels, and additionally BC6H cubemap
	void BlockCompress(const vz::graphics::Texture& texture_src, const vz::graphics::Texture& texture_bc, vz::graphics::CommandList cmd, uint32_t dst_slice_offset = 0);

	enum BORDEREXPANDSTYLE
	{
		BORDEREXPAND_DISABLE,
		BORDEREXPAND_WRAP,
		BORDEREXPAND_CLAMP,
	};
	// Performs copy operation even between different texture formats
	//	NOTE: DstMIP can be specified as -1 to use main subresource, otherwise the subresource (>=0) must have been generated explicitly!
	//	Can also expand border region according to desired sampler func
	void CopyTexture2D(
		const vz::graphics::Texture& dst, int DstMIP, int DstX, int DstY,
		const vz::graphics::Texture& src, int SrcMIP, int SrcX, int SrcY,
		vz::graphics::CommandList cmd,
		BORDEREXPANDSTYLE borderExpand = BORDEREXPAND_DISABLE,
		bool srgb_convert = false
	);

	void SetShadowProps2D(int max_resolution);
	void SetShadowPropsCube(int max_resolution);


	void SetWireRender(bool value);
	bool IsWireRender();
	
	void SetToDrawDebugCameras(bool param);
	bool GetToDrawDebugCameras();
	bool GetToDrawGridHelper();
	void SetToDrawGridHelper(bool value);
	void SetShadowsEnabled(bool value);
	bool IsShadowsEnabled();
	
	void Workaround(const int bug, vz::graphics::CommandList cmd);

	// Gets pick ray according to the current screen resolution and pointer coordinates. Can be used as input into RayIntersectWorld()
	vz::primitive::Ray GetPickRay(long cursorX, long cursorY, const vz::Canvas& canvas, const vz::scene::CameraComponent& camera = vz::scene::GetCamera());


	// Add box to render in next frame. It will be rendered in DrawDebugWorld()
	void DrawBox(const XMFLOAT4X4& boxMatrix, const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1), bool depth = true);
	// Add sphere to render in next frame. It will be rendered in DrawDebugWorld()
	void DrawSphere(const vz::primitive::Sphere& sphere, const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1), bool depth = true);
	// Add capsule to render in next frame. It will be rendered in DrawDebugWorld()
	void DrawCapsule(const vz::primitive::Capsule& capsule, const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1), bool depth = true);

	struct RenderableLine
	{
		XMFLOAT3 start = XMFLOAT3(0, 0, 0);
		XMFLOAT3 end = XMFLOAT3(0, 0, 0);
		XMFLOAT4 color_start = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 color_end = XMFLOAT4(1, 1, 1, 1);
	};
	// Add line to render in the next frame. It will be rendered in DrawDebugWorld()
	void DrawLine(const RenderableLine& line, bool depth = false);

	struct RenderableLine2D
	{
		XMFLOAT2 start = XMFLOAT2(0, 0);
		XMFLOAT2 end = XMFLOAT2(0, 0);
		XMFLOAT4 color_start = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 color_end = XMFLOAT4(1, 1, 1, 1);
	};
	// Add 2D line to render in the next frame. It will be rendered in DrawDebugWorld() in screen space
	void DrawLine(const RenderableLine2D& line);

	void DrawAxis(const XMMATRIX& matrix, float size, bool depth = false);

	struct RenderablePoint
	{
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		float size = 1.0f;
		XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
	};
	// Add point to render in the next frame. It will be rendered in DrawDebugWorld() as an X
	void DrawPoint(const RenderablePoint& point, bool depth = false);

	struct RenderableTriangle
	{
		XMFLOAT3 positionA = XMFLOAT3(0, 0, 0);
		XMFLOAT4 colorA = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT3 positionB = XMFLOAT3(0, 0, 0);
		XMFLOAT4 colorB = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT3 positionC = XMFLOAT3(0, 0, 0);
		XMFLOAT4 colorC = XMFLOAT4(1, 1, 1, 1);
	};
	// Add triangle to render in the next frame. It will be rendered in DrawDebugWorld()
	void DrawTriangle(const RenderableTriangle& triangle, bool wireframe = false, bool depth = true);

	struct DebugTextParams
	{
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		int pixel_height = 32;
		float scaling = 1;
		XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
		enum FLAGS // do not change values, it's bound to lua manually!
		{
			NONE = 0,
			DEPTH_TEST = 1 << 0,		// text can be occluded by geometry
			CAMERA_FACING = 1 << 1,		// text will be rotated to face the camera
			CAMERA_SCALING = 1 << 2,	// text will be always the same size, independent of distance to camera
		};
		uint32_t flags = NONE;
	};
	// Add text to render in the next frame. It will be rendered in DrawDebugWorld()
	//	The memory to text doesn't need to be retained by the caller, as it will be copied internally
	void DrawDebugText(const char* text, const DebugTextParams& params);

	struct PaintRadius
	{
		vz::ecs::Entity objectEntity = vz::ecs::INVALID_ENTITY;
		int subset = -1;
		uint32_t uvset = 0;
		float radius = 0;
		XMUINT2 center = {};
		XMUINT2 dimensions = {};
		float rotation = 0;
		uint shape = 0; // 0: circle, 1 : square
	};
	void DrawPaintRadius(const PaintRadius& paintrad);

	struct PaintTextureParams
	{
		vz::graphics::Texture editTex; // UAV writable texture
		vz::graphics::Texture brushTex; // splat texture (optional)
		vz::graphics::Texture revealTex; // mask texture that can be revealed (optional)
		PaintTexturePushConstants push = {}; // shader parameters
	};
	void PaintIntoTexture(const PaintTextureParams& params);
	vz::Resource CreatePaintableTexture(uint32_t width, uint32_t height, uint32_t mips = 0, vz::Color initialColor = vz::Color::Transparent());

	// Add a texture that should be mipmapped whenever it is feasible to do so
	void AddDeferredMIPGen(const vz::graphics::Texture& texture, bool preserve_coverage = false);
	void AddDeferredBlockCompression(const vz::graphics::Texture& texture_src, const vz::graphics::Texture& texture_bc);

	struct CustomShader
	{
		std::string name;
		uint32_t filterMask = vz::enums::FILTER_OPAQUE;
		vz::graphics::PipelineState pso[vz::enums::RENDERPASS_COUNT] = {};
	};
	// Registers a custom shader that can be set to materials. 
	//	Returns the ID of the custom shader that can be used with MaterialComponent::SetCustomShaderID()
	int RegisterCustomShader(const CustomShader& customShader);
	const vz::vector<CustomShader>& GetCustomShaders();

	// Thread-local barrier batching helpers:
	void PushBarrier(const vz::graphics::GPUBarrier& barrier);
	void FlushBarriers(vz::graphics::CommandList cmd);

};

