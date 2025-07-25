#include "Renderer.h"
#include "ShaderEngine.h"
#include "Image.h"
#include "Font.h"
#include "SortLib.h"
#include "GPUBVH.h"
#include "ShaderLoader.h"
#include "../Shaders/ShaderInterop.h"

#include "sheenLUT.h"

#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/JobSystem.h"
#include "Utils/EventHandler.h"
#include "Utils/Config.h"
#include "Utils/Helpers.h"

#include <memory>

using namespace vz::graphics;

namespace vz::renderer
{
	// NOTE: these options affect not only rendering effects but also scene resource generation
	float giBoost = 1.f;
	float renderingSpeed = 1.f;
	bool isOcclusionCullingEnabled = true;
	bool isFreezeCullingCameraEnabled = false;
	bool isWetmapRefreshEnabled = false;
	bool isSceneUpdateEnabled = true;
	bool isTemporalAAEnabled = true;
	bool isTemporalAADebugEnabled = false;
	bool isDisableAlbedoMaps = false;
	bool isForceDiffuseLighting = false;
	bool isTessellationEnabled = false;
	bool isVolumeLightsEnabled = false;
	bool isLensFlareEnabled = false;
	bool isLightShaftsEnabled = false;
	bool isFSREnabled = false;
	bool isTonemapping = true;
	bool isWireRender = false;
	bool isDebugLightCulling = false;
	bool isAdvancedLightCulling = false;
	bool isMeshShaderAllowed = false;
	bool isShadowsEnabled = true;
	bool isShadowLODOverride = true;
	bool isVariableRateShadingClassification = false;
	bool isSurfelGIDebugEnabled = false;
	bool isColorGradingEnabled = false;
	bool isGaussianSplattingEnabled = true;

	bool isDebugShapeEnabled = true;
	bool isDebugShapeCleanStart = true;
}

namespace vz::renderer 
{
	RasterizerState		rasterizers[RSTYPE_COUNT];		// engine
	DepthStencilState	depthStencils[DSSTYPE_COUNT];	// engine
	BlendState			blendStates[BSTYPE_COUNT];		// engine
	Sampler				samplers[SAMPLER_COUNT];		// engine
	// must be released
	GPUBuffer			buffers[BUFFERTYPE_COUNT];		// engine
	Texture				textures[TEXTYPE_COUNT];		// engine

	GPUBuffer			indirectDebugStatsReadback[GraphicsDevice::GetBufferCount()];
	bool				indirectDebugStatsReadback_available[GraphicsDevice::GetBufferCount()] = {};

	// progressive components
	std::vector<Entity> deferredGeometryGPUBVHGens;								// engine
	std::vector<std::pair<Texture, bool>> deferredMIPGens;						// engine
	std::vector<std::pair<Texture, Texture>> deferredBCQueue;					// engine, BC : Block Compression
	std::vector<std::pair<Texture, Texture>> deferredTextureCopy;				// engine
	std::vector<std::pair<GPUBuffer, std::pair<void*, size_t>>> deferredBufferUpdate;	// engine

	std::mutex deferredResourceMutex;
}

namespace vz::renderer
{
	static std::atomic_bool initialized{ false };
	bool IsInitialized()
	{
		return initialized.load();
	}

	uint32_t ComputeObjectLODForView(const RenderableComponent& renderable, const geometrics::AABB& aabb, const GeometryComponent& geometry, const XMMATRIX& ViewProjection)
	{
		XMFLOAT4 rect = aabb.ProjectToScreen(ViewProjection);
		float width = rect.z - rect.x;
		float height = rect.w - rect.y;
		float maxdim = std::max(width, height);
		float lod_max = float(geometry.GetLODCount() - 1);
		float lod = clamp(std::log2(1.0f / maxdim) + renderable.GetLODBias(), 0.0f, lod_max);
		return uint32_t(lod);
	}

	GraphicsDevice*& device = GetDevice();

	void LoadBuffers()
	{
		GPUBufferDesc bd;
		bd.usage = Usage::DEFAULT;
		bd.size = sizeof(FrameCB);
		bd.bind_flags = BindFlag::CONSTANT_BUFFER;
		device->CreateBuffer(&bd, nullptr, &buffers[BUFFERTYPE_FRAMECB]);
		device->SetName(&buffers[BUFFERTYPE_FRAMECB], "buffers[BUFFERTYPE_FRAMECB]");

		bd.size = sizeof(IndirectDrawArgsInstanced) + (sizeof(XMFLOAT4) + sizeof(XMFLOAT4)) * 1000;
		bd.bind_flags = BindFlag::VERTEX_BUFFER | BindFlag::UNORDERED_ACCESS;
		bd.misc_flags = ResourceMiscFlag::BUFFER_RAW | ResourceMiscFlag::INDIRECT_ARGS;
		device->CreateBuffer(&bd, nullptr, &buffers[BUFFERTYPE_INDIRECT_DEBUG_0]);
		device->SetName(&buffers[BUFFERTYPE_INDIRECT_DEBUG_0], "buffers[BUFFERTYPE_INDIRECT_DEBUG_0]");
		device->CreateBuffer(&bd, nullptr, &buffers[BUFFERTYPE_INDIRECT_DEBUG_1]);
		device->SetName(&buffers[BUFFERTYPE_INDIRECT_DEBUG_1], "buffers[BUFFERTYPE_INDIRECT_DEBUG_1]");

		bd.size = sizeof(IndirectDrawArgsInstanced);
		bd.usage = Usage::READBACK;
		bd.bind_flags = {};
		bd.misc_flags = {};
		IndirectDrawArgsInstanced initdata_readback = {};
		for (auto& buf : indirectDebugStatsReadback)
		{
			device->CreateBuffer(&bd, &initdata_readback, &buf);
			device->SetName(&buf, "indirectDebugStatsReadback");
		}

		// to do (future features)
		{
			TextureDesc desc;
			desc.bind_flags = BindFlag::SHADER_RESOURCE;
			desc.format = Format::R8_UNORM;
			desc.height = 16;
			desc.width = 16;
			SubresourceData InitData;
			InitData.data_ptr = sheenLUTdata;
			InitData.row_pitch = desc.width;
			device->CreateTexture(&desc, &InitData, &textures[TEXTYPE_2D_SHEENLUT]);
			device->SetName(&textures[TEXTYPE_2D_SHEENLUT], "textures[TEXTYPE_2D_SHEENLUT]");
		}
	}
	void SetUpStates()
	{
		RasterizerState rs;
		rs.fill_mode = FillMode::SOLID;
		rs.cull_mode = CullMode::BACK;
		rs.front_counter_clockwise = true;
		rs.depth_bias = 0;
		rs.depth_bias_clamp = 0;
		rs.slope_scaled_depth_bias = 0;
		rs.depth_clip_enable = true;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
		rs.conservative_rasterization_enable = false;
		rasterizers[RSTYPE_FRONT] = rs;

		rs.fill_mode = FillMode::SOLID;
		rs.cull_mode = CullMode::BACK;
		rs.front_counter_clockwise = true;
		rs.depth_bias = -1;
		rs.depth_bias_clamp = 0;
		rs.slope_scaled_depth_bias = -4.0f;
		rs.depth_clip_enable = false;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
		rs.conservative_rasterization_enable = false;
		rasterizers[RSTYPE_SHADOW] = rs;
		rs.cull_mode = CullMode::NONE;
		rasterizers[RSTYPE_SHADOW_DOUBLESIDED] = rs;

		rs.fill_mode = FillMode::WIREFRAME;
		rs.cull_mode = CullMode::BACK;
		rs.front_counter_clockwise = true;
		rs.depth_bias = 0;
		rs.depth_bias_clamp = 0;
		rs.slope_scaled_depth_bias = 0;
		rs.depth_clip_enable = true;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
		rs.conservative_rasterization_enable = false;
		rasterizers[RSTYPE_WIRE] = rs;
		rs.antialiased_line_enable = true;
		rasterizers[RSTYPE_WIRE_SMOOTH] = rs;

		rs.fill_mode = FillMode::SOLID;
		rs.cull_mode = CullMode::NONE;
		rs.front_counter_clockwise = true;
		rs.depth_bias = 0;
		rs.depth_bias_clamp = 0;
		rs.slope_scaled_depth_bias = 0;
		rs.depth_clip_enable = true;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
		rs.conservative_rasterization_enable = false;
		rasterizers[RSTYPE_DOUBLESIDED] = rs;

		rs.fill_mode = FillMode::WIREFRAME;
		rs.cull_mode = CullMode::NONE;
		rs.front_counter_clockwise = true;
		rs.depth_bias = 0;
		rs.depth_bias_clamp = 0;
		rs.slope_scaled_depth_bias = 0;
		rs.depth_clip_enable = true;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
		rs.conservative_rasterization_enable = false;
		rasterizers[RSTYPE_WIRE_DOUBLESIDED] = rs;
		rs.antialiased_line_enable = true;
		rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH] = rs;

		rs.fill_mode = FillMode::SOLID;
		rs.cull_mode = CullMode::FRONT;
		rs.front_counter_clockwise = true;
		rs.depth_bias = 0;
		rs.depth_bias_clamp = 0;
		rs.slope_scaled_depth_bias = 0;
		rs.depth_clip_enable = true;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
		rs.conservative_rasterization_enable = false;
		rasterizers[RSTYPE_BACK] = rs;

		rs.fill_mode = FillMode::SOLID;
		rs.cull_mode = CullMode::NONE;
		rs.front_counter_clockwise = true;
		rs.depth_bias = 1;
		rs.depth_bias_clamp = 0.01f;
		rs.slope_scaled_depth_bias = 0;
		rs.depth_clip_enable = false;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
		rs.conservative_rasterization_enable = false;
		rasterizers[RSTYPE_OCCLUDEE] = rs;

		rs.fill_mode = FillMode::SOLID;
		rs.cull_mode = CullMode::FRONT;
		rs.front_counter_clockwise = true;
		rs.depth_bias = 0;
		rs.depth_bias_clamp = 0;
		rs.slope_scaled_depth_bias = 0;
		rs.depth_clip_enable = false;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
		rs.conservative_rasterization_enable = false;
		rasterizers[RSTYPE_SKY] = rs;

		rs.fill_mode = FillMode::SOLID;
		rs.cull_mode = CullMode::NONE;
		rs.front_counter_clockwise = true;
		rs.depth_bias = 0;
		rs.depth_bias_clamp = 0;
		rs.slope_scaled_depth_bias = 0;
		rs.depth_clip_enable = true;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
#ifdef VOXELIZATION_CONSERVATIVE_RASTERIZATION_ENABLED
		if (device->CheckCapability(GraphicsDeviceCapability::CONSERVATIVE_RASTERIZATION))
		{
			rs.conservative_rasterization_enable = true;
		}
		else
#endif // VOXELIZATION_CONSERVATIVE_RASTERIZATION_ENABLED
		{
			rs.forced_sample_count = 8;
		}
		rasterizers[RSTYPE_VOXELIZE] = rs;


		DepthStencilState dsd;
		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ALL;
		dsd.depth_func = ComparisonFunc::GREATER;

		dsd.stencil_enable = true;
		dsd.stencil_read_mask = 0;
		dsd.stencil_write_mask = 0xFF;
		dsd.front_face.stencil_func = ComparisonFunc::ALWAYS;
		dsd.front_face.stencil_pass_op = StencilOp::REPLACE;
		dsd.front_face.stencil_fail_op = StencilOp::KEEP;
		dsd.front_face.stencil_depth_fail_op = StencilOp::KEEP;
		dsd.back_face.stencil_func = ComparisonFunc::ALWAYS;
		dsd.back_face.stencil_pass_op = StencilOp::REPLACE;
		dsd.back_face.stencil_fail_op = StencilOp::KEEP;
		dsd.back_face.stencil_depth_fail_op = StencilOp::KEEP;
		depthStencils[DSSTYPE_DEFAULT] = dsd;

		dsd.depth_func = ComparisonFunc::GREATER_EQUAL;
		depthStencils[DSSTYPE_TRANSPARENT] = dsd;
		dsd.depth_func = ComparisonFunc::GREATER;

		dsd.depth_write_mask = DepthWriteMask::ZERO;
		depthStencils[DSSTYPE_HOLOGRAM] = dsd;

		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ALL;
		dsd.depth_func = ComparisonFunc::GREATER;
		dsd.stencil_enable = false;
		depthStencils[DSSTYPE_SHADOW] = dsd;

		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ALL;
		dsd.depth_func = ComparisonFunc::GREATER;
		dsd.stencil_enable = false;
		depthStencils[DSSTYPE_CAPTUREIMPOSTOR] = dsd;


		dsd.depth_enable = true;
		dsd.stencil_enable = false;
		dsd.depth_write_mask = DepthWriteMask::ZERO;
		dsd.depth_func = ComparisonFunc::GREATER_EQUAL;
		depthStencils[DSSTYPE_DEPTHREAD] = dsd;

		dsd.depth_enable = false;
		dsd.stencil_enable = false;
		depthStencils[DSSTYPE_DEPTHDISABLED] = dsd;


		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ZERO;
		dsd.depth_func = ComparisonFunc::EQUAL;
		depthStencils[DSSTYPE_DEPTHREADEQUAL] = dsd;


		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ALL;
		dsd.depth_func = ComparisonFunc::GREATER;
		depthStencils[DSSTYPE_ENVMAP] = dsd;

		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ALL;
		dsd.depth_func = ComparisonFunc::ALWAYS;
		dsd.stencil_enable = false;
		depthStencils[DSSTYPE_WRITEONLY] = dsd;

		dsd.depth_enable = false;
		dsd.depth_write_mask = DepthWriteMask::ZERO;
		dsd.stencil_enable = true;
		dsd.stencil_read_mask = 0;
		dsd.front_face.stencil_func = ComparisonFunc::ALWAYS;
		dsd.front_face.stencil_pass_op = StencilOp::REPLACE;
		dsd.back_face = dsd.front_face;
		for (int i = 0; i < 8; ++i)
		{
			dsd.stencil_write_mask = uint8_t(1 << i);
			depthStencils[DSSTYPE_COPY_STENCIL_BIT_0 + i] = dsd;
		}


		BlendState bd;
		bd.render_target[0].blend_enable = false;
		bd.render_target[0].render_target_write_mask = ColorWrite::ENABLE_ALL;
		bd.alpha_to_coverage_enable = false;
		bd.independent_blend_enable = false;
		blendStates[BSTYPE_OPAQUE] = bd;

		bd.render_target[0].src_blend = Blend::SRC_ALPHA;
		bd.render_target[0].dest_blend = Blend::INV_SRC_ALPHA;
		bd.render_target[0].blend_op = BlendOp::ADD;
		bd.render_target[0].src_blend_alpha = Blend::ONE;
		bd.render_target[0].dest_blend_alpha = Blend::INV_SRC_ALPHA;
		bd.render_target[0].blend_op_alpha = BlendOp::ADD;
		bd.render_target[0].blend_enable = true;
		bd.render_target[0].render_target_write_mask = ColorWrite::ENABLE_ALL;
		bd.alpha_to_coverage_enable = false;
		bd.independent_blend_enable = false;
		blendStates[BSTYPE_TRANSPARENT] = bd;

		bd.render_target[0].blend_enable = true;
		bd.render_target[0].src_blend = Blend::ONE;
		bd.render_target[0].dest_blend = Blend::INV_SRC_ALPHA;
		bd.render_target[0].blend_op = BlendOp::ADD;
		bd.render_target[0].src_blend_alpha = Blend::ONE;
		bd.render_target[0].dest_blend_alpha = Blend::INV_SRC_ALPHA;
		bd.render_target[0].blend_op_alpha = BlendOp::ADD;
		bd.render_target[0].render_target_write_mask = ColorWrite::ENABLE_ALL;
		bd.independent_blend_enable = false;
		bd.alpha_to_coverage_enable = false;
		blendStates[BSTYPE_PREMULTIPLIED] = bd;


		bd.render_target[0].blend_enable = true;
		bd.render_target[0].src_blend = Blend::SRC_ALPHA;
		bd.render_target[0].dest_blend = Blend::ONE;
		bd.render_target[0].blend_op = BlendOp::ADD;
		bd.render_target[0].src_blend_alpha = Blend::ZERO;
		bd.render_target[0].dest_blend_alpha = Blend::ONE;
		bd.render_target[0].blend_op_alpha = BlendOp::ADD;
		bd.render_target[0].render_target_write_mask = ColorWrite::ENABLE_ALL;
		bd.independent_blend_enable = false,
			bd.alpha_to_coverage_enable = false;
		blendStates[BSTYPE_ADDITIVE] = bd;


		bd.render_target[0].blend_enable = false;
		bd.render_target[0].render_target_write_mask = ColorWrite::DISABLE;
		bd.independent_blend_enable = false,
			bd.alpha_to_coverage_enable = false;
		blendStates[BSTYPE_COLORWRITEDISABLE] = bd;

		bd.render_target[0].src_blend = Blend::DEST_COLOR;
		bd.render_target[0].dest_blend = Blend::ZERO;
		bd.render_target[0].blend_op = BlendOp::ADD;
		bd.render_target[0].src_blend_alpha = Blend::DEST_ALPHA;
		bd.render_target[0].dest_blend_alpha = Blend::ZERO;
		bd.render_target[0].blend_op_alpha = BlendOp::ADD;
		bd.render_target[0].blend_enable = true;
		bd.render_target[0].render_target_write_mask = ColorWrite::ENABLE_ALL;
		bd.alpha_to_coverage_enable = false;
		bd.independent_blend_enable = false;
		blendStates[BSTYPE_MULTIPLY] = bd;

		bd.render_target[0].src_blend = Blend::ZERO;
		bd.render_target[0].dest_blend = Blend::SRC_COLOR;
		bd.render_target[0].blend_op = BlendOp::ADD;
		bd.render_target[0].src_blend_alpha = Blend::ONE;
		bd.render_target[0].dest_blend_alpha = Blend::ONE;
		bd.render_target[0].blend_op_alpha = BlendOp::MAX;
		bd.render_target[0].blend_enable = true;
		bd.render_target[0].render_target_write_mask = ColorWrite::ENABLE_ALL;
		bd.alpha_to_coverage_enable = false;
		bd.independent_blend_enable = false;
		blendStates[BSTYPE_TRANSPARENTSHADOW] = bd;





		SamplerDesc samplerDesc;
		samplerDesc.filter = Filter::MIN_MAG_MIP_LINEAR;
		samplerDesc.address_u = TextureAddressMode::MIRROR;
		samplerDesc.address_v = TextureAddressMode::MIRROR;
		samplerDesc.address_w = TextureAddressMode::MIRROR;
		samplerDesc.mip_lod_bias = 0.0f;
		samplerDesc.max_anisotropy = 0;
		samplerDesc.comparison_func = ComparisonFunc::NEVER;
		samplerDesc.border_color = SamplerBorderColor::TRANSPARENT_BLACK;
		samplerDesc.min_lod = 0;
		samplerDesc.max_lod = std::numeric_limits<float>::max();
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_LINEAR_MIRROR]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_LINEAR;
		samplerDesc.address_u = TextureAddressMode::CLAMP;
		samplerDesc.address_v = TextureAddressMode::CLAMP;
		samplerDesc.address_w = TextureAddressMode::CLAMP;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_LINEAR_CLAMP]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_LINEAR;
		samplerDesc.address_u = TextureAddressMode::WRAP;
		samplerDesc.address_v = TextureAddressMode::WRAP;
		samplerDesc.address_w = TextureAddressMode::WRAP;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_LINEAR_WRAP]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_LINEAR;
		samplerDesc.address_u = TextureAddressMode::BORDER;
		samplerDesc.address_v = TextureAddressMode::BORDER;
		samplerDesc.address_w = TextureAddressMode::BORDER;
		samplerDesc.border_color = SamplerBorderColor::TRANSPARENT_BLACK;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_LINEAR_BORDER]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_POINT;
		samplerDesc.address_u = TextureAddressMode::MIRROR;
		samplerDesc.address_v = TextureAddressMode::MIRROR;
		samplerDesc.address_w = TextureAddressMode::MIRROR;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_POINT_MIRROR]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_POINT;
		samplerDesc.address_u = TextureAddressMode::WRAP;
		samplerDesc.address_v = TextureAddressMode::WRAP;
		samplerDesc.address_w = TextureAddressMode::WRAP;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_POINT_WRAP]);


		samplerDesc.filter = Filter::MIN_MAG_MIP_POINT;
		samplerDesc.address_u = TextureAddressMode::CLAMP;
		samplerDesc.address_v = TextureAddressMode::CLAMP;
		samplerDesc.address_w = TextureAddressMode::CLAMP;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_POINT_CLAMP]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_POINT;
		samplerDesc.address_u = TextureAddressMode::BORDER;
		samplerDesc.address_v = TextureAddressMode::BORDER;
		samplerDesc.address_w = TextureAddressMode::BORDER;
		samplerDesc.border_color = SamplerBorderColor::TRANSPARENT_BLACK;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_POINT_BORDER]);

		samplerDesc.filter = Filter::ANISOTROPIC;
		samplerDesc.address_u = TextureAddressMode::CLAMP;
		samplerDesc.address_v = TextureAddressMode::CLAMP;
		samplerDesc.address_w = TextureAddressMode::CLAMP;
		samplerDesc.max_anisotropy = 16;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_ANISO_CLAMP]);

		samplerDesc.filter = Filter::ANISOTROPIC;
		samplerDesc.address_u = TextureAddressMode::WRAP;
		samplerDesc.address_v = TextureAddressMode::WRAP;
		samplerDesc.address_w = TextureAddressMode::WRAP;
		samplerDesc.max_anisotropy = 16;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_ANISO_WRAP]);

		samplerDesc.filter = Filter::ANISOTROPIC;
		samplerDesc.address_u = TextureAddressMode::MIRROR;
		samplerDesc.address_v = TextureAddressMode::MIRROR;
		samplerDesc.address_w = TextureAddressMode::MIRROR;
		samplerDesc.max_anisotropy = 16;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_ANISO_MIRROR]);

		samplerDesc.filter = Filter::ANISOTROPIC;
		samplerDesc.address_u = TextureAddressMode::WRAP;
		samplerDesc.address_v = TextureAddressMode::WRAP;
		samplerDesc.address_w = TextureAddressMode::WRAP;
		samplerDesc.max_anisotropy = 16;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_OBJECTSHADER]);

		samplerDesc.filter = Filter::COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		samplerDesc.address_u = TextureAddressMode::CLAMP;
		samplerDesc.address_v = TextureAddressMode::CLAMP;
		samplerDesc.address_w = TextureAddressMode::CLAMP;
		samplerDesc.mip_lod_bias = 0.0f;
		samplerDesc.max_anisotropy = 0;
		samplerDesc.comparison_func = ComparisonFunc::GREATER_EQUAL;
		device->CreateSampler(&samplerDesc, &samplers[SAMPLER_CMP_DEPTH]);
	}

	bool Initialize()
	{
		Timer timer;

		renderer::SetUpStates();	// depthStencils, blendStates, samplers
		renderer::LoadBuffers();	// buffers, textures

		static eventhandler::Handle handle2 = eventhandler::Subscribe(eventhandler::EVENT_RELOAD_SHADERS, [](uint64_t userdata) { LoadShaders(); });

		jobsystem::context ctx;
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { image::Initialize(); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { font::Initialize(); });
		//jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { shader::Initialize(); });
		shader::Initialize(); // GG
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { gpusortlib::Initialize(); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { gpubvh::Initialize(); });

		jobsystem::Wait(ctx);
		backlog::post("Shader Engine Initialized (" + std::to_string((int)std::round(timer.elapsed())) + " ms)", backlog::LogLevel::Info);

		renderer::initialized.store(true);
		return true;
	}
	void Deinitialize()
	{
		jobsystem::WaitAllJobs();

		ReleaseRenderRes(buffers, BUFFERTYPE_COUNT);
		ReleaseRenderRes(textures, TEXTYPE_COUNT);

		ReleaseRenderRes(rasterizers, SAMPLER_COUNT);
		ReleaseRenderRes(depthStencils, SAMPLER_COUNT);
		ReleaseRenderRes(blendStates, SAMPLER_COUNT);
		ReleaseRenderRes(samplers, SAMPLER_COUNT);

		ReleaseRenderRes(indirectDebugStatsReadback, GraphicsDevice::GetBufferCount());

		deferredTextureCopy.clear();
		deferredBufferUpdate.clear();
		deferredBCQueue.clear();
		deferredMIPGens.clear();
		deferredGeometryGPUBVHGens.clear();

		shader::Deinitialize();
		image::Deinitialize();
		font::Deinitialize();
		gpubvh::Deinitialize();
		gpusortlib::Deinitialize();
		renderer::initialized.store(false);
	}
}



namespace vz
{
	using namespace graphics;

	bool Initialize(graphics::GraphicsDevice* device)
	{
		std::string version = vz::GetComponentVersion();
		assert(version == vz::COMPONENT_INTERFACE_VERSION);

		graphics::GetDevice() = device;

		return true;
	}

	bool LoadRenderer()
	{
		return renderer::Initialize();
	}

	bool ApplyConfiguration()
	{
		renderer::isTemporalAAEnabled = config::GetBoolConfig("SHADER_ENGINE_SETTINGS", "TEMPORAL_AA");
		renderer::isGaussianSplattingEnabled = config::GetBoolConfig("SHADER_ENGINE_SETTINGS", "GAUSSIAN_SPLATTING");
		renderer::isTonemapping = config::GetBoolConfig("SHADER_ENGINE_SETTINGS", "TONEMAPPING");
		renderer::isShadowsEnabled = config::GetBoolConfig("SHADER_ENGINE_SETTINGS", "SHADOW_ENABLED");

		renderer::isDebugLightCulling = config::GetBoolConfig("DEBUG_SETTINGS", "LIGHT_CULLING");
		renderer::isDebugShapeEnabled = config::GetBoolConfig("DEBUG_SETTINGS", "DEBUG_SHAPE");
		renderer::isDebugShapeCleanStart = config::GetBoolConfig("DEBUG_SETTINGS", "DEBUG_SHAPE_CLEAN_START");

		return true;
	}

	void Deinitialize()
	{
		renderer::Deinitialize();
	}
}