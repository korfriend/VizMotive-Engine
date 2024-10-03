#include "GraphicsInterface.h"
#include "Shaders/ShaderInterop.h"
#include "Components/GComponents.h"
#include "Utils/JobSystem.h"
#include "Utils/Timer.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"
#include "Libs/Math.h"
#include "Renderer.h"

namespace vz::graphics::common
{
	InputLayout			inputLayouts[ILTYPE_COUNT];
	RasterizerState		rasterizers[RSTYPE_COUNT];
	DepthStencilState	depthStencils[DSSTYPE_COUNT];
	BlendState			blendStates[BSTYPE_COUNT];
	Shader				shaders[SHADERTYPE_COUNT];
	GPUBuffer			buffers[BUFFERTYPE_COUNT];
	Sampler				samplers[SAMPLER_COUNT];

	PipelineState		PSO_debug[DEBUGRENDERING_COUNT];
	PipelineState		PSO_mesh[RENDERPASS_COUNT];
	PipelineState		PSO_wireframe;
}

namespace vz::graphics
{
	struct GSceneDetails : GScene
	{
		GSceneDetails(Scene* scene) : GScene(scene) {}

		// note all GPU resources (their pointers) are managed by
		//  ComPtr or 
		//  RAII (Resource Acquisition Is Initialization) patterns

		// * note:: resources... 개별 할당된 상태...
		// * srv, uav... slot 이 bindless 한가...
		// * 여기선 scene 단위로 bindless resources
		// * - texture2D and texture 3D
		// * - constant buffers (instances)
		// * - vertex buffers ... , index buffer 만 IA 로 pipeline 에 적용

		// Instances for bindless renderables:
		//	contains in order:
		//		1) renderables (normal meshes)
		size_t instanceArraySize = 0;
		graphics::GPUBuffer instanceUploadBuffer[graphics::GraphicsDevice::GetBufferCount()]; // dynamic GPU-usage
		graphics::GPUBuffer instanceBuffer;	// default GPU-usage
		ShaderMeshInstance* instanceArrayMapped = nullptr; // CPU-access buffer pointer for instanceUploadBuffer[%2]

		// Materials for bindless visibility indexing:
		size_t materialArraySize = 0;
		graphics::GPUBuffer materialUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		graphics::GPUBuffer materialBuffer;
		graphics::GPUBuffer textureStreamingFeedbackBuffer;	// a sinlge UINT
		graphics::GPUBuffer textureStreamingFeedbackBuffer_readback[graphics::GraphicsDevice::GetBufferCount()];
		const uint32_t* textureStreamingFeedbackMapped = nullptr;
		ShaderMaterial* materialArrayMapped = nullptr;

		// 2. advanced version (based on WickedEngine)
		//ShaderMeshInstance* instanceArrayMapped = nullptr;
		//size_t instanceArraySize = 0;
		//graphics::GPUBuffer geometryUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		//graphics::GPUBuffer....
		//graphics::Texture....

		bool Update(const float dt) override;
		bool Destory() override;
	};

	bool GSceneDetails::Update(const float dt)
	{

		jobsystem::context ctx;

		GraphicsDevice* device = GetGraphicsDevice();

		// 1. dynamic rendering (such as particles and terrain, cloud...) kickoff
		// TODO

		// 2. constant buffers for renderables
		instanceArraySize = scene_->GetRenderableCount();
		if (instanceUploadBuffer[0].desc.size < (instanceArraySize * sizeof(ShaderMeshInstance)))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderMeshInstance);
			desc.size = desc.stride * instanceArraySize * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::SHADER_RESOURCE;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			// if CACHE_COHERENT_UMA is allowed, then use instanceUploadBuffer directly.
			// otherwise, use instanceBuffer.
			if (!device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
			{
				// Non-UMA: separate Default usage buffer
				device->CreateBuffer(&desc, nullptr, &instanceBuffer);
				device->SetName(&instanceBuffer, "GSceneDetails::instanceBuffer");

				// Upload buffer shouldn't be used by shaders with Non-UMA:
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
			}

			desc.usage = Usage::UPLOAD;
			for (int i = 0; i < arraysize(instanceUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &instanceUploadBuffer[i]);
				device->SetName(&instanceUploadBuffer[i], "GSceneDetails::instanceUploadBuffer");
			}
		}
		instanceArrayMapped = (ShaderMeshInstance*)instanceUploadBuffer[device->GetBufferIndex()].mapped_data;

		// 3. material buffers for shaders
		std::vector<Entity> mat_entities;
		std::vector<MaterialComponent*> mat_components;
		materialArraySize = compfactory::GetMaterialComponents(mat_entities, mat_components);
		if (materialUploadBuffer[0].desc.size < (materialArraySize * sizeof(ShaderMaterial)))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderMaterial);
			desc.size = desc.stride * materialArraySize * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::SHADER_RESOURCE;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			if (!device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
			{
				// Non-UMA: separate Default usage buffer
				device->CreateBuffer(&desc, nullptr, &materialBuffer);
				device->SetName(&materialBuffer, "GSceneDetails::materialBuffer");

				// Upload buffer shouldn't be used by shaders with Non-UMA:
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
			}

			desc.usage = Usage::UPLOAD;
			for (int i = 0; i < arraysize(materialUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &materialUploadBuffer[i]);
				device->SetName(&materialUploadBuffer[i], "GSceneDetails::materialUploadBuffer");
			}
		}
		materialArrayMapped = (ShaderMaterial*)materialUploadBuffer[device->GetBufferIndex()].mapped_data;

		if (textureStreamingFeedbackBuffer.desc.size < materialArraySize * sizeof(uint32_t))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(uint32_t);
			desc.size = desc.stride * materialArraySize * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::UNORDERED_ACCESS;
			desc.format = Format::R32_UINT;
			device->CreateBuffer(&desc, nullptr, &textureStreamingFeedbackBuffer);
			device->SetName(&textureStreamingFeedbackBuffer, "GSceneDetails::textureStreamingFeedbackBuffer");

			// Readback buffer shouldn't be used by shaders:
			desc.usage = Usage::READBACK;
			desc.bind_flags = BindFlag::NONE;
			desc.misc_flags = ResourceMiscFlag::NONE;
			for (int i = 0; i < arraysize(materialUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &textureStreamingFeedbackBuffer_readback[i]);
				device->SetName(&textureStreamingFeedbackBuffer_readback[i], "GSceneDetails::textureStreamingFeedbackBuffer_readback");
			}
		}
		textureStreamingFeedbackMapped = (const uint32_t*)textureStreamingFeedbackBuffer_readback[device->GetBufferIndex()].mapped_data;

		return true;
	}

	bool GSceneDetails::Destory()
	{
		return true;
	}

}

namespace vz::graphics
{
	struct GRenderPath3DDetails : GRenderPath3D
	{
		GRenderPath3DDetails(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal) : GRenderPath3D(swapChain, rtRenderFinal) {}

		// resources associated with render target buffers and textures

		bool ResizeCanvas() override; // must delete all canvas-related resources and re-create
		bool Render() override;
		bool Destory() override;
	};

	bool GRenderPath3DDetails::ResizeCanvas()
	{
		return true;
	}

	bool GRenderPath3DDetails::Render()
	{
		GraphicsDevice*& device = GetDevice();
		CommandList cmd = device->BeginCommandList();
		//RenderPassImage rp[] = {
		//	RenderPassImage::RenderTarget(&rtOut, RenderPassImage::LoadOp::CLEAR),
		//
		//};
		//graphicsDevice_->RenderPassBegin(rp, arraysize(rp), cmd);
		device->RenderPassBegin(&swapChain_, cmd);
		device->RenderPassEnd(cmd);
		device->SubmitCommandLists();

		return true;
	}

	bool GRenderPath3DDetails::Destory()
	{
		return true;
	}

}

namespace vz::graphics
{
	GScene* NewGScene(Scene* scene)
	{
		return new GSceneDetails(scene);
	}

	GRenderPath3D* NewGRenderPath(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
	{
		return new GRenderPath3DDetails(swapChain, rtRenderFinal);
	}

	bool InitRendererShaders()
	{
		Timer timer;

		initializer::SetUpStates();
		initializer::LoadBuffers();

		//static eventhandler::Handle handle2 = eventhandler::Subscribe(eventhandler::EVENT_RELOAD_SHADERS, [](uint64_t userdata) { LoadShaders(); });
		//LoadShaders();

		backlog::post("renderer Initialized (" + std::to_string((int)std::round(timer.elapsed())) + " ms)", backlog::LogLevel::Info);
		//initialized.store(true);
		return true;
	}

	bool LoadShader(
		graphics::ShaderStage stage,
		graphics::Shader& shader,
		const std::string& filename,
		graphics::ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines)
	{
		return shader::LoadShader(stage, shader, filename, minshadermodel, permutation_defines);
	}
}

