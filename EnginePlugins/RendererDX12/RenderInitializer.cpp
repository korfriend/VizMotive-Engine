#include "Renderer.h"
#include "Image.h"
#include "PluginInterface.h"
#include "Shaders/ShaderInterop.h"

#include "sheenLUT.h"

#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/JobSystem.h"
#include "Utils/EventHandler.h"

namespace vz::rcommon
{
	extern InputLayout			inputLayouts[ILTYPE_COUNT];
	extern RasterizerState		rasterizers[RSTYPE_COUNT];
	extern DepthStencilState	depthStencils[DSSTYPE_COUNT];
	extern BlendState			blendStates[BSTYPE_COUNT];
	extern Shader				shaders[SHADERTYPE_COUNT];
	extern GPUBuffer			buffers[BUFFERTYPE_COUNT];
	extern Sampler				samplers[SAMPLER_COUNT];
	extern Texture				textures[TEXTYPE_COUNT];

	extern PipelineState		PSO_debug[DEBUGRENDERING_COUNT];
	extern PipelineState		PSO_wireframe;
	extern PipelineState		PSO_occlusionquery;

	extern std::unordered_map<uint32_t, PipelineState> PSO_render[RENDERPASS_COUNT][SHADERTYPE_BIN_COUNT];

	extern std::vector<std::pair<Texture, bool>> deferredMIPGens;
	extern std::vector<std::pair<Texture, Texture>> deferredBCQueue; // BC : Block Compression
	extern std::vector<std::pair<Texture, Texture>> deferredTextureCopy;
	extern std::vector<std::pair<GPUBuffer, std::pair<void*, size_t>>> deferredBufferUpdate;
}



namespace vz::initializer
{
	using namespace vz::graphics;

	static std::atomic_bool initialized{ false };

	GraphicsDevice*& device = GetDevice();

	void LoadBuffers()
	{
		GPUBufferDesc bd;
		bd.usage = Usage::DEFAULT;
		bd.size = sizeof(FrameCB);
		bd.bind_flags = BindFlag::CONSTANT_BUFFER;
		device->CreateBuffer(&bd, nullptr, &rcommon::buffers[BUFFERTYPE_FRAMECB]);
		device->SetName(&rcommon::buffers[BUFFERTYPE_FRAMECB], "buffers[BUFFERTYPE_FRAMECB]");

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
			device->CreateTexture(&desc, &InitData, &rcommon::textures[TEXTYPE_2D_SHEENLUT]);
			device->SetName(&rcommon::textures[TEXTYPE_2D_SHEENLUT], "textures[TEXTYPE_2D_SHEENLUT]");
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
		rcommon::rasterizers[RSTYPE_FRONT] = rs;

		rs.fill_mode = FillMode::SOLID;
		rs.cull_mode = CullMode::BACK;
		rs.front_counter_clockwise = true;
		rs.depth_bias = -1;
		rs.depth_bias_clamp = 0;
		rs.slope_scaled_depth_bias = -4.0f;
		rs.depth_clip_enable = true;
		rs.multisample_enable = false;
		rs.antialiased_line_enable = false;
		rs.conservative_rasterization_enable = false;
		rcommon::rasterizers[RSTYPE_SHADOW] = rs;
		rs.cull_mode = CullMode::NONE;
		rcommon::rasterizers[RSTYPE_SHADOW_DOUBLESIDED] = rs;

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
		rcommon::rasterizers[RSTYPE_WIRE] = rs;
		rs.antialiased_line_enable = true;
		rcommon::rasterizers[RSTYPE_WIRE_SMOOTH] = rs;

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
		rcommon::rasterizers[RSTYPE_DOUBLESIDED] = rs;

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
		rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED] = rs;
		rs.antialiased_line_enable = true;
		rcommon::rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH] = rs;

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
		rcommon::rasterizers[RSTYPE_BACK] = rs;

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
		rcommon::rasterizers[RSTYPE_OCCLUDEE] = rs;

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
		rcommon::rasterizers[RSTYPE_SKY] = rs;

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
		rcommon::rasterizers[RSTYPE_VOXELIZE] = rs;


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
		rcommon::depthStencils[DSSTYPE_DEFAULT] = dsd;

		dsd.depth_func = ComparisonFunc::GREATER_EQUAL;
		rcommon::depthStencils[DSSTYPE_TRANSPARENT] = dsd;
		dsd.depth_func = ComparisonFunc::GREATER;

		dsd.depth_write_mask = DepthWriteMask::ZERO;
		rcommon::depthStencils[DSSTYPE_HOLOGRAM] = dsd;

		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ALL;
		dsd.depth_func = ComparisonFunc::GREATER;
		dsd.stencil_enable = false;
		rcommon::depthStencils[DSSTYPE_SHADOW] = dsd;

		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ALL;
		dsd.depth_func = ComparisonFunc::GREATER;
		dsd.stencil_enable = false;
		rcommon::depthStencils[DSSTYPE_CAPTUREIMPOSTOR] = dsd;


		dsd.depth_enable = true;
		dsd.stencil_enable = false;
		dsd.depth_write_mask = DepthWriteMask::ZERO;
		dsd.depth_func = ComparisonFunc::GREATER_EQUAL;
		rcommon::depthStencils[DSSTYPE_DEPTHREAD] = dsd;

		dsd.depth_enable = false;
		dsd.stencil_enable = false;
		rcommon::depthStencils[DSSTYPE_DEPTHDISABLED] = dsd;


		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ZERO;
		dsd.depth_func = ComparisonFunc::EQUAL;
		rcommon::depthStencils[DSSTYPE_DEPTHREADEQUAL] = dsd;


		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ALL;
		dsd.depth_func = ComparisonFunc::GREATER;
		rcommon::depthStencils[DSSTYPE_ENVMAP] = dsd;

		dsd.depth_enable = true;
		dsd.depth_write_mask = DepthWriteMask::ALL;
		dsd.depth_func = ComparisonFunc::ALWAYS;
		dsd.stencil_enable = false;
		rcommon::depthStencils[DSSTYPE_WRITEONLY] = dsd;

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
			rcommon::depthStencils[DSSTYPE_COPY_STENCIL_BIT_0 + i] = dsd;
		}


		BlendState bd;
		bd.render_target[0].blend_enable = false;
		bd.render_target[0].render_target_write_mask = ColorWrite::ENABLE_ALL;
		bd.alpha_to_coverage_enable = false;
		bd.independent_blend_enable = false;
		rcommon::blendStates[BSTYPE_OPAQUE] = bd;

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
		rcommon::blendStates[BSTYPE_TRANSPARENT] = bd;

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
		rcommon::blendStates[BSTYPE_PREMULTIPLIED] = bd;


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
		rcommon::blendStates[BSTYPE_ADDITIVE] = bd;


		bd.render_target[0].blend_enable = false;
		bd.render_target[0].render_target_write_mask = ColorWrite::DISABLE;
		bd.independent_blend_enable = false,
			bd.alpha_to_coverage_enable = false;
		rcommon::blendStates[BSTYPE_COLORWRITEDISABLE] = bd;

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
		rcommon::blendStates[BSTYPE_MULTIPLY] = bd;

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
		rcommon::blendStates[BSTYPE_TRANSPARENTSHADOW] = bd;





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
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_LINEAR_MIRROR]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_LINEAR;
		samplerDesc.address_u = TextureAddressMode::CLAMP;
		samplerDesc.address_v = TextureAddressMode::CLAMP;
		samplerDesc.address_w = TextureAddressMode::CLAMP;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_LINEAR_CLAMP]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_LINEAR;
		samplerDesc.address_u = TextureAddressMode::WRAP;
		samplerDesc.address_v = TextureAddressMode::WRAP;
		samplerDesc.address_w = TextureAddressMode::WRAP;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_LINEAR_WRAP]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_POINT;
		samplerDesc.address_u = TextureAddressMode::MIRROR;
		samplerDesc.address_v = TextureAddressMode::MIRROR;
		samplerDesc.address_w = TextureAddressMode::MIRROR;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_POINT_MIRROR]);

		samplerDesc.filter = Filter::MIN_MAG_MIP_POINT;
		samplerDesc.address_u = TextureAddressMode::WRAP;
		samplerDesc.address_v = TextureAddressMode::WRAP;
		samplerDesc.address_w = TextureAddressMode::WRAP;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_POINT_WRAP]);


		samplerDesc.filter = Filter::MIN_MAG_MIP_POINT;
		samplerDesc.address_u = TextureAddressMode::CLAMP;
		samplerDesc.address_v = TextureAddressMode::CLAMP;
		samplerDesc.address_w = TextureAddressMode::CLAMP;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_POINT_CLAMP]);

		samplerDesc.filter = Filter::ANISOTROPIC;
		samplerDesc.address_u = TextureAddressMode::CLAMP;
		samplerDesc.address_v = TextureAddressMode::CLAMP;
		samplerDesc.address_w = TextureAddressMode::CLAMP;
		samplerDesc.max_anisotropy = 16;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_ANISO_CLAMP]);

		samplerDesc.filter = Filter::ANISOTROPIC;
		samplerDesc.address_u = TextureAddressMode::WRAP;
		samplerDesc.address_v = TextureAddressMode::WRAP;
		samplerDesc.address_w = TextureAddressMode::WRAP;
		samplerDesc.max_anisotropy = 16;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_ANISO_WRAP]);

		samplerDesc.filter = Filter::ANISOTROPIC;
		samplerDesc.address_u = TextureAddressMode::MIRROR;
		samplerDesc.address_v = TextureAddressMode::MIRROR;
		samplerDesc.address_w = TextureAddressMode::MIRROR;
		samplerDesc.max_anisotropy = 16;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_ANISO_MIRROR]);

		samplerDesc.filter = Filter::ANISOTROPIC;
		samplerDesc.address_u = TextureAddressMode::WRAP;
		samplerDesc.address_v = TextureAddressMode::WRAP;
		samplerDesc.address_w = TextureAddressMode::WRAP;
		samplerDesc.max_anisotropy = 16;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_OBJECTSHADER]);

		samplerDesc.filter = Filter::COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		samplerDesc.address_u = TextureAddressMode::CLAMP;
		samplerDesc.address_v = TextureAddressMode::CLAMP;
		samplerDesc.address_w = TextureAddressMode::CLAMP;
		samplerDesc.mip_lod_bias = 0.0f;
		samplerDesc.max_anisotropy = 0;
		samplerDesc.comparison_func = ComparisonFunc::GREATER_EQUAL;
		device->CreateSampler(&samplerDesc, &rcommon::samplers[SAMPLER_CMP_DEPTH]);
	}
	bool IsInitialized()
	{
		return initialized.load();
	}

	void ReleaseResources()
	{
#define ReleaseRenderRes(SRC, R_COUNT) for (size_t i = 0, n = (size_t)R_COUNT; i < n; ++i) rcommon::SRC[i] = {};

		jobsystem::WaitAllJobs();
		ReleaseRenderRes(shaders, SHADERTYPE_COUNT);
		ReleaseRenderRes(buffers, BUFFERTYPE_COUNT);
		ReleaseRenderRes(samplers, SAMPLER_COUNT);
		ReleaseRenderRes(textures, TEXTYPE_COUNT);
		ReleaseRenderRes(PSO_debug, DEBUGRENDERING_COUNT);
		rcommon::PSO_wireframe = {};
		rcommon::PSO_occlusionquery = {};

		rcommon::deferredTextureCopy.clear();
		rcommon::deferredBufferUpdate.clear();
		rcommon::deferredBCQueue.clear();
		rcommon::deferredMIPGens.clear();

		for (size_t i = 0, n = (size_t)RENDERPASS_COUNT; i < n; ++i)
		{
			for (size_t j = 0; j < (size_t)SHADERTYPE_BIN_COUNT; ++j)
			{
				rcommon::PSO_render[i][j].clear();
			}
		}

		image::Deinitialize();
	}
}

namespace vz
{
	bool InitRenderer()
	{
		Timer timer;

		initializer::SetUpStates();
		initializer::LoadBuffers();

		static eventhandler::Handle handle2 = eventhandler::Subscribe(eventhandler::EVENT_RELOAD_SHADERS, [](uint64_t userdata) { LoadShaders(); });
		
		jobsystem::context ctx;
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { shader::LoadShaders(); });
		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { image::Initialize(); });

		jobsystem::Wait(ctx);
		backlog::post("renderer Initialized (" + std::to_string((int)std::round(timer.elapsed())) + " ms)", backlog::LogLevel::Info);
		initializer::initialized.store(true);
		return true;
	}
}
