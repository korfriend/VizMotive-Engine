#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::Update_DDGI(CommandList cmd)
	{
		if (!scene_Gdetails->TLAS.IsValid() && !scene_Gdetails->sceneBVH.IsValid())
			return;

		if (renderer::DDGI_RAYCOUNT == 0)
			return;

		GSceneDetails::DDGI& scene_ddgi = scene_Gdetails->ddgi;
		if (!scene_ddgi.rayBuffer.IsValid())
			return;
		
		shader::WaitShaderLoad_RayTracing();

		auto prof_range = profiler::BeginRangeGPU("DDGI", &cmd);
		device->EventBegin("DDGI", cmd);

		if (scene_ddgi.frameIndex == 0)
		{
			device->Barrier(GPUBarrier::Image(&scene_ddgi.depthTexture, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::UNORDERED_ACCESS), cmd);
			device->Barrier(GPUBarrier::Buffer(&scene_ddgi.probeBuffer, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::UNORDERED_ACCESS), cmd);
			device->Barrier(GPUBarrier::Buffer(&scene_ddgi.varianceBuffer, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::UNORDERED_ACCESS), cmd);
			device->ClearUAV(&scene_ddgi.probeBuffer, 0, cmd);
			device->ClearUAV(&scene_ddgi.depthTexture, 0, cmd);
			device->ClearUAV(&scene_ddgi.varianceBuffer, 0, cmd);
			device->ClearUAV(&scene_ddgi.rayallocationBuffer, 0, cmd);
			device->ClearUAV(&scene_ddgi.raycountBuffer, 0, cmd);
			device->ClearUAV(&scene_ddgi.rayBuffer, 0, cmd);
			device->Barrier(GPUBarrier::Memory(), cmd);
			device->Barrier(GPUBarrier::Image(&scene_ddgi.depthTexture, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE), cmd);
			device->Barrier(GPUBarrier::Buffer(&scene_ddgi.probeBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE), cmd);
			device->Barrier(GPUBarrier::Buffer(&scene_ddgi.varianceBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE), cmd);
		}

		BindCommonResources(cmd);

		DDGIPushConstants push;
		uint8_t instanceInclusionMask = 0xFF;
		push.instanceInclusionMask = instanceInclusionMask;
		push.frameIndex = scene_ddgi.frameIndex;
		push.rayCount = std::min(renderer::DDGI_RAYCOUNT, DDGI_MAX_RAYCOUNT);
		push.blendSpeed = renderer::DDGI_BLEND_SPEED;

		const uint probe_count = scene_Gdetails->shaderscene.ddgi.probe_count;

		// Ray allocation:
		{
			device->EventBegin("Ray allocation", cmd);

			device->BindComputeShader(&shaders[CSTYPE_DDGI_RAYALLOCATION], cmd);
			device->PushConstants(&push, sizeof(push), cmd);

			const GPUResource* res[] = {
				&scene_ddgi.varianceBuffer,
			};
			device->BindResources(res, 0, arraysize(res), cmd);

			const GPUResource* uavs[] = {
				&scene_ddgi.rayallocationBuffer,
				&scene_ddgi.raycountBuffer,
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			device->ClearUAV(&scene_ddgi.rayallocationBuffer, 0, cmd);
			device->Barrier(GPUBarrier::Memory(&scene_ddgi.rayallocationBuffer), cmd);

			device->Dispatch(probe_count, 1, 1, cmd);

			device->EventEnd(cmd);
		}

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Memory(&scene_ddgi.rayallocationBuffer),
				GPUBarrier::Buffer(&scene_ddgi.raycountBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		// Indirect prepare:
		{
			device->EventBegin("Indirect prepare", cmd);

			device->BindComputeShader(&shaders[CSTYPE_DDGI_INDIRECTPREPARE], cmd);

			const GPUResource* uavs[] = {
				&scene_ddgi.rayallocationBuffer,
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			device->Dispatch(1, 1, 1, cmd);

			device->EventEnd(cmd);
		}

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Buffer(&scene_ddgi.rayallocationBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::INDIRECT_ARGUMENT | ResourceState::SHADER_RESOURCE_COMPUTE),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		// Raytracing:
		{
			device->EventBegin("Raytrace", cmd);

			device->BindComputeShader(&shaders[CSTYPE_DDGI_RAYTRACE], cmd);
			device->PushConstants(&push, sizeof(push), cmd);

			MiscCB cb = {};
			float angle = random::GetRandom(0.0f, 1.0f) * XM_2PI;
			XMVECTOR axis = XMVectorSet(
				random::GetRandom(-1.0f, 1.0f),
				random::GetRandom(-1.0f, 1.0f),
				random::GetRandom(-1.0f, 1.0f),
				0
			);
			axis = XMVector3Normalize(axis);
			XMStoreFloat4x4(&cb.g_xTransform, XMMatrixRotationAxis(axis, angle));
			device->BindDynamicConstantBuffer(cb, CB_GETBINDSLOT(MiscCB), cmd);

			const GPUResource* res[] = {
				&scene_ddgi.rayallocationBuffer,
				&scene_ddgi.raycountBuffer,
			};
			device->BindResources(res, 0, arraysize(res), cmd);

			const GPUResource* uavs[] = {
				&scene_ddgi.rayBuffer,
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			device->DispatchIndirect(&scene_ddgi.rayallocationBuffer, 0, cmd);

			device->EventEnd(cmd);
		}

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Buffer(&scene_ddgi.rayBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE),
				GPUBarrier::Image(&scene_ddgi.depthTexture, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::UNORDERED_ACCESS),
				GPUBarrier::Buffer(&scene_ddgi.varianceBuffer, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::UNORDERED_ACCESS),
				//GPUBarrier::Buffer(&scene_ddgi.raycountBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE),
				GPUBarrier::Buffer(&scene_ddgi.probeBuffer, ResourceState::SHADER_RESOURCE_COMPUTE, ResourceState::UNORDERED_ACCESS),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		// Update:
		{
			device->EventBegin("Update", cmd);

			device->BindComputeShader(&shaders[CSTYPE_DDGI_UPDATE], cmd);
			device->PushConstants(&push, sizeof(push), cmd);

			const GPUResource* res[] = {
				&scene_ddgi.rayBuffer,
				&scene_ddgi.raycountBuffer,
			};
			device->BindResources(res, 0, arraysize(res), cmd);

			const GPUResource* uavs[] = {
				&scene_ddgi.varianceBuffer,
				&scene_ddgi.probeBuffer,
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			device->Dispatch(probe_count, 1, 1, cmd);

			device->EventEnd(cmd);
		}

		// Update Depth:
		{
			device->EventBegin("Update Depth", cmd);

			device->BindComputeShader(&shaders[CSTYPE_DDGI_UPDATE_DEPTH], cmd);
			device->PushConstants(&push, sizeof(push), cmd);

			const GPUResource* res[] = {
				&scene_ddgi.rayBuffer,
				&scene_ddgi.raycountBuffer,
			};
			device->BindResources(res, 0, arraysize(res), cmd);

			const GPUResource* uavs[] = {
				&scene_ddgi.depthTexture,
				&scene_ddgi.probeBuffer,
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			device->Dispatch(probe_count, 1, 1, cmd);

			device->EventEnd(cmd);
		}

		{
			GPUBarrier barriers[] = {
				GPUBarrier::Buffer(&scene_ddgi.probeBuffer, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE),
				GPUBarrier::Image(&scene_ddgi.depthTexture, ResourceState::UNORDERED_ACCESS, ResourceState::SHADER_RESOURCE_COMPUTE),
			};
			device->Barrier(barriers, arraysize(barriers), cmd);
		}

		profiler::EndRange(prof_range);
		device->EventEnd(cmd);
	}
}