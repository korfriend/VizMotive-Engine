#include "GPUBVH.h"
#include "../Shaders/ShaderInterop_BVH.h"

#include "ShaderLoader.h"
#include "SortLib.h"

#include "Components/GComponents.h"

#include "Utils/Profiler.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"
#include "Utils/Timer.h"

namespace vz::gpubvh
{
	using GPrimBuffers = GGeometryComponent::GPrimBuffers;
	using Primitive = GeometryComponent::Primitive;
	using GPrimEffectBuffers = GRenderableComponent::GPrimEffectBuffers;
	using namespace vz::graphics;


	static Shader bvhPrimitives_geometryspace;
	static Shader bvhPrimitives;
	static Shader bvhHierarchy;
	static Shader bvhPropagateAABB;

	void LoadShaders()
	{
		shader::LoadShader(ShaderStage::CS, bvhPrimitives_geometryspace, "bvh_primitivesCS_geometryspace.cso");
		shader::LoadShader(ShaderStage::CS, bvhPrimitives, "bvh_primitivesCS.cso");
		shader::LoadShader(ShaderStage::CS, bvhHierarchy, "bvh_hierarchyCS.cso");
		shader::LoadShader(ShaderStage::CS, bvhPropagateAABB, "bvh_propagateaabbCS.cso");
	}

	void Initialize()
	{
		Timer timer;

		static eventhandler::Handle handle = eventhandler::Subscribe(eventhandler::EVENT_RELOAD_SHADERS, [](uint64_t userdata) { LoadShaders(); });
		LoadShaders();

		vzlog("vz::gpubvh Initialized (%d ms)", (int)std::round(timer.elapsed()));
	}
	void Deinitialize()
	{
		bvhPrimitives_geometryspace = {};
		bvhPrimitives = {};
		bvhHierarchy = {};
		bvhPropagateAABB = {};
	}

	bool UpdateGeometryGPUBVH(const Entity geometryEntity, graphics::CommandList cmd)
	{
		GraphicsDevice* device = GetDevice();

		GGeometryComponent* geometry = (GGeometryComponent*)compfactory::GetGeometryComponent(geometryEntity);
		assert(geometry);

		if (geometry->GetNumParts() == 0 || !geometry->HasRenderData())
		{
			return false;
		}

		using Primitive = GeometryComponent::Primitive;
		using BVHBuffers = GGeometryComponent::BVHBuffers;

		const std::vector<Primitive>& parts = geometry->GetPrimitives();

		size_t bvh_parts = 0;
		for (size_t i = 0; i < parts.size(); ++i)
		{
			const Primitive& prim = parts[i];
			GPrimBuffers* part_buffers = geometry->GetGPrimBuffer(i);
			if (prim.GetPrimitiveType() != GeometryComponent::PrimitiveType::TRIANGLES
				|| part_buffers == nullptr)
			{
				continue;
			}

			uint totalTriangles = (uint)prim.GetNumIndices() / 3;

			BVHBuffers& bvhBuffers = part_buffers->bvhBuffers;
			if (totalTriangles > 0 && !bvhBuffers.primitiveCounterBuffer.IsValid())
			{
				GPUBufferDesc desc;
				desc.bind_flags = BindFlag::SHADER_RESOURCE;
				desc.stride = sizeof(uint);
				desc.size = desc.stride;
				desc.misc_flags = ResourceMiscFlag::BUFFER_RAW;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &bvhBuffers.primitiveCounterBuffer);
				device->SetName(&bvhBuffers.primitiveCounterBuffer, "GPUBVH::primitiveCounterBuffer");
			}
			else
			{
				bvhBuffers.primitiveCounterBuffer = {};
			}

			if (totalTriangles > bvhBuffers.primitiveCapacity)
			{
				bvhBuffers.primitiveCapacity = std::max(2u, totalTriangles);

				GPUBufferDesc desc;

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(BVHNode);
				desc.size = desc.stride * bvhBuffers.primitiveCapacity * 2;
				desc.misc_flags = ResourceMiscFlag::BUFFER_RAW;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &bvhBuffers.bvhNodeBuffer);
				device->SetName(&bvhBuffers.bvhNodeBuffer, "GPUBVH::BVHNodeBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(uint);
				desc.size = desc.stride * bvhBuffers.primitiveCapacity * 2;
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &bvhBuffers.bvhParentBuffer);
				device->SetName(&bvhBuffers.bvhParentBuffer, "GPUBVH::BVHParentBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(uint);
				desc.size = desc.stride * (((bvhBuffers.primitiveCapacity - 1) + 31) / 32); // bitfield for internal nodes
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &bvhBuffers.bvhFlagBuffer);
				device->SetName(&bvhBuffers.bvhFlagBuffer, "GPUBVH::BVHFlagBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(uint);
				desc.size = desc.stride * bvhBuffers.primitiveCapacity;
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &bvhBuffers.primitiveIDBuffer);
				device->SetName(&bvhBuffers.primitiveIDBuffer, "GPUBVH::primitiveIDBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(BVHPrimitive);
				desc.size = desc.stride * bvhBuffers.primitiveCapacity;
				desc.misc_flags = ResourceMiscFlag::BUFFER_RAW;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &bvhBuffers.primitiveBuffer);
				device->SetName(&bvhBuffers.primitiveBuffer, "GPUBVH::primitiveBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(float); // morton buffer is float because sorting must be done and gpu sort operates on floats for now!
				desc.size = desc.stride * bvhBuffers.primitiveCapacity;
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &bvhBuffers.primitiveMortonBuffer);
				device->SetName(&bvhBuffers.primitiveMortonBuffer, "GPUBVH::primitiveMortonBuffer");
			}


			// ----- build ----- 
			auto range = profiler::BeginRangeGPU("BVH Rebuild", &cmd);

			uint32_t primitiveCount = totalTriangles;

			GPUResource unbind;
			const GPUResource* uavs_unbind[4] = { &unbind , &unbind , &unbind , &unbind };

			device->EventBegin("BVH - Primitive (GEOMETRY-ONLY) Builder", cmd);
			{
				device->BindComputeShader(&bvhPrimitives_geometryspace, cmd);
				const GPUResource* uavs[] = {
					&bvhBuffers.primitiveIDBuffer,
					&bvhBuffers.primitiveBuffer,
					&bvhBuffers.primitiveMortonBuffer,
				};
				device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

				BVHPushConstants push;
				push.geometryIndex = geometry->geometryOffset;
				push.subsetIndex = i;
				push.primitiveCount = primitiveCount;
				push.instanceIndex = 0; // geometry-binding BVH does NOT require instance!
				push.vb_pos_w = part_buffers->vbPosW.descriptor_srv;
				push.ib = part_buffers->ib.descriptor_srv;

				const geometrics::AABB& aabb = prim.GetAABB();
				push.aabb_min = aabb.getMin();
				push.aabb_extents_rcp = aabb.getWidth();
				push.aabb_extents_rcp.x = 1.f / push.aabb_extents_rcp.x;
				push.aabb_extents_rcp.y = 1.f / push.aabb_extents_rcp.y;
				push.aabb_extents_rcp.z = 1.f / push.aabb_extents_rcp.z;

				device->PushConstants(&push, sizeof(push), cmd);

				device->Dispatch(
					(push.primitiveCount + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE,
					1,
					1,
					cmd
				);
				device->BindUAVs(uavs_unbind, 0, arraysize(uavs_unbind), cmd);

				GPUBarrier barriers[] = {
					GPUBarrier::Memory()
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&bvhBuffers.primitiveCounterBuffer, ResourceState::SHADER_RESOURCE, ResourceState::COPY_DST),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}
			device->UpdateBuffer(&bvhBuffers.primitiveCounterBuffer, &primitiveCount, cmd);
			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&bvhBuffers.primitiveCounterBuffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);

			device->EventBegin("BVH - Sort Primitive Mortons", cmd);
			gpusortlib::Sort(primitiveCount, gpusortlib::COMPARISON_FLOAT, bvhBuffers.primitiveMortonBuffer, bvhBuffers.primitiveCounterBuffer, 0, bvhBuffers.primitiveIDBuffer, cmd);
			device->EventEnd(cmd);

			device->EventBegin("BVH - Build Hierarchy", cmd);
			{
				device->BindComputeShader(&bvhHierarchy, cmd);
				const GPUResource* uavs[] = {
					&bvhBuffers.bvhNodeBuffer,
					&bvhBuffers.bvhParentBuffer,
					&bvhBuffers.bvhFlagBuffer
				};
				device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

				const GPUResource* res[] = {
					&bvhBuffers.primitiveCounterBuffer,
					&bvhBuffers.primitiveIDBuffer,
					&bvhBuffers.primitiveMortonBuffer,
				};
				device->BindResources(res, 0, arraysize(res), cmd);

				device->Dispatch((primitiveCount + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1, cmd);

				device->BindUAVs(uavs_unbind, 0, arraysize(uavs_unbind), cmd);
				device->BindResources(uavs_unbind, 0, arraysize(uavs_unbind), cmd);

				GPUBarrier barriers[] = {
					GPUBarrier::Memory()
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}
			device->EventEnd(cmd);

			device->EventBegin("BVH - Propagate AABB", cmd);
			{
				// for example in depth of 4
				// 
				// Level 3:     A 
				//            /   \
				// Level 2:  B     C
				//          / \   / \
				// Level 1: D   E F   G
				//         /\  /\ /\  /\
				// Level 0: Leaf nodes
				//
				// First pass:
				// When D's children arrive at D -> one child returns, but the other completes D and can propagate to B
				// When E's children arrive at E -> one child returns, the other completes E
				// When F's children arrive at F -> one child returns, but the other completes F and can propagate to C
				// When G's children arrive at G -> one child returns, the other completes G
				//
				// Second pass:
				// Propagation from E to B (B is complete)
				// Propagation from G to C(C is complete)
				// ...and I said a third pass would be needed for B and C to propagate to A, but actually either B or C can reach A in the second pass!
				// 
				// In my implementation, for robustness propagation of AABBs, use MAX tree depth!

				uint treeDepth = (uint)ceil(log2(primitiveCount));

				GPUBarrier barriers[] = {
					GPUBarrier::Memory()
				};
				device->Barrier(barriers, arraysize(barriers), cmd);

				device->BindComputeShader(&bvhPropagateAABB, cmd);
				const GPUResource* uavs[] = {
					&bvhBuffers.bvhNodeBuffer,
					&bvhBuffers.bvhFlagBuffer,
				};
				device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

				const GPUResource* res[] = {
					&bvhBuffers.primitiveCounterBuffer,
					&bvhBuffers.primitiveIDBuffer,
					&bvhBuffers.primitiveBuffer,
					&bvhBuffers.bvhParentBuffer,
				};
				device->BindResources(res, 0, arraysize(res), cmd);

				for (int i = 0; i < treeDepth; i++) {
					device->Dispatch((primitiveCount + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1, cmd);
				}

				device->BindUAVs(uavs_unbind, 0, arraysize(uavs_unbind), cmd);
				device->BindResources(uavs_unbind, 0, arraysize(uavs_unbind), cmd);

				device->Barrier(barriers, arraysize(barriers), cmd);

				NameComponent* name = compfactory::GetNameComponent(geometryEntity);
				vzlog("GPUBVH Command for (%s) is submitted. (Max TreeDepth: % d)", name->GetName().c_str(), treeDepth);
			}
			device->EventEnd(cmd);

			profiler::EndRange(range); // BVH rebuild
			bvh_parts++;
		}

		if (bvh_parts > 0)
		{
			geometry->timeStampGPUBVHUpdate = TimerNow;
		}

		return true;
	}

	bool UpdateSceneGPUBVH(const Entity sceneEntity)
	{
		//sceneShader.geometrybuffer
		vzlog_assert(0, "TODO");
		return true;
	}
}