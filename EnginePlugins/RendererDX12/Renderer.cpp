#include "GraphicsInterface.h"
#include "Renderer.h"

namespace vz::graphics
{
	struct GSceneDetails : GScene
	{
		graphics::GPUBuffer instanceUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];
		ShaderMeshInstance* instanceArrayMapped = nullptr;
		size_t instanceArraySize = 0;

		graphics::GPUBuffer geometryUploadBuffer[graphics::GraphicsDevice::GetBufferCount()];

		bool Update(const float dt) override;
	};

	bool GScene::Update(const float dt)
	{
		dt_ += dt;

		// 1. transform component updates
		// 2. animation updates
		// 3. dynamic rendering (such as particles and terrain, cloud...) kickoff
		// 

		jobsystem::context ctx;

		instanceArraySize = renderables_.size();
		if (instanceUploadBuffer[0].desc.size < (instanceArraySize * sizeof(ShaderMeshInstance)))
		{
			GPUBufferDesc desc;
			desc.stride = sizeof(ShaderMeshInstance);
			desc.size = desc.stride * instanceArraySize * 2; // *2 to grow fast
			desc.bind_flags = BindFlag::SHADER_RESOURCE;
			desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
			if (!device->CheckCapability(GraphicsDeviceCapability::CACHE_COHERENT_UMA))
			{
				// Non-UMA: separate Default usage buffer
				device->CreateBuffer(&desc, nullptr, &instanceBuffer);
				device->SetName(&instanceBuffer, "Scene::instanceBuffer");

				// Upload buffer shouldn't be used by shaders with Non-UMA:
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
			}

			desc.usage = Usage::UPLOAD;
			for (int i = 0; i < arraysize(instanceUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &instanceUploadBuffer[i]);
				device->SetName(&instanceUploadBuffer[i], "Scene::instanceUploadBuffer");
			}
		}
		instanceArrayMapped = (ShaderMeshInstance*)instanceUploadBuffer[device->GetBufferIndex()].mapped_data;

		materialArraySize = materials.GetCount();
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
				device->SetName(&materialBuffer, "Scene::materialBuffer");

				// Upload buffer shouldn't be used by shaders with Non-UMA:
				desc.bind_flags = BindFlag::NONE;
				desc.misc_flags = ResourceMiscFlag::NONE;
			}

			desc.usage = Usage::UPLOAD;
			for (int i = 0; i < arraysize(materialUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &materialUploadBuffer[i]);
				device->SetName(&materialUploadBuffer[i], "Scene::materialUploadBuffer");
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
			device->SetName(&textureStreamingFeedbackBuffer, "Scene::textureStreamingFeedbackBuffer");

			// Readback buffer shouldn't be used by shaders:
			desc.usage = Usage::READBACK;
			desc.bind_flags = BindFlag::NONE;
			desc.misc_flags = ResourceMiscFlag::NONE;
			for (int i = 0; i < arraysize(materialUploadBuffer); ++i)
			{
				device->CreateBuffer(&desc, nullptr, &textureStreamingFeedbackBuffer_readback[i]);
				device->SetName(&textureStreamingFeedbackBuffer_readback[i], "Scene::textureStreamingFeedbackBuffer_readback");
			}
		}
		textureStreamingFeedbackMapped = (const uint32_t*)textureStreamingFeedbackBuffer_readback[device->GetBufferIndex()].mapped_data;

		RunTransformUpdateSystem(ctx);

		wi::jobsystem::Wait(ctx); // dependencies

		RunHierarchyUpdateSystem(ctx);
	}
}
