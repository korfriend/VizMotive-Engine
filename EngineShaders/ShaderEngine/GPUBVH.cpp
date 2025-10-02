#include "GPUBVH.h"
#include "../Shaders/ShaderInterop_BVH.h"

#include "Scene_Detail.h"
#include "ShaderLoader.h"
#include "SortLib.h"

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

	bool UpdateGeometryGPUBVH(GGeometryComponent* geometry, graphics::CommandList cmd)
	{
		GraphicsDevice* device = GetDevice();
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

			uint total_tris = (uint)prim.GetNumIndices() / 3;

			BVHBuffers& bvhBuffers = part_buffers->bvhBuffers;
			if (total_tris > 0 && !bvhBuffers.primitiveCounterBuffer.IsValid())
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

			if (total_tris == 0)
			{
				bvhBuffers.primitiveCounterBuffer = {};
			}

			if (total_tris > bvhBuffers.primitiveCapacity)
			{
				bvhBuffers.primitiveCapacity = std::max(2u, total_tris);

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
			auto range = profiler::BeginRangeGPU("BVH Rebuild (Geometry)", &cmd);

			uint32_t primitive_count = total_tris;

			GPUResource unbind;
			const GPUResource* res_unbind[4] = { &unbind , &unbind , &unbind , &unbind };

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
				push.primitiveCount = primitive_count;
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
				device->BindUAVs(res_unbind, 0, arraysize(res_unbind), cmd);

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
			device->UpdateBuffer(&bvhBuffers.primitiveCounterBuffer, &primitive_count, cmd);
			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&bvhBuffers.primitiveCounterBuffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}

			device->EventEnd(cmd);

			device->EventBegin("BVH - Sort Primitive Mortons", cmd);
			gpusortlib::Sort(primitive_count, gpusortlib::COMPARISON_FLOAT, bvhBuffers.primitiveMortonBuffer, bvhBuffers.primitiveCounterBuffer, 0, bvhBuffers.primitiveIDBuffer, cmd);
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

				device->Dispatch((primitive_count + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1, cmd);

				device->BindUAVs(res_unbind, 0, arraysize(res_unbind), cmd);
				device->BindResources(res_unbind, 0, arraysize(res_unbind), cmd);

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

				uint treeDepth = (uint)ceil(log2(primitive_count));

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
					device->Dispatch((primitive_count + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1, cmd);
				}

				device->BindUAVs(res_unbind, 0, arraysize(res_unbind), cmd);
				device->BindResources(res_unbind, 0, arraysize(res_unbind), cmd);

				device->Barrier(barriers, arraysize(barriers), cmd);

				NameComponent* name = compfactory::GetNameComponent(geometry->GetEntity());
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

	bool UpdateSceneGPUBVH(GScene* scene, graphics::CommandList cmd)
	{
		using Primitive = GeometryComponent::Primitive;
		using BVHBuffers = GGeometryComponent::BVHBuffers; // GPUBVH 

		renderer::GSceneDetails* scene_Gdetails = (renderer::GSceneDetails*)scene;
		GraphicsDevice* device = scene_Gdetails->device;
		BVHBuffers& scene_bvh = scene_Gdetails->sceneBVH;

		size_t bvh_parts = 0;

		std::vector<GRenderableComponent*>& renderables = scene_Gdetails->renderableComponents;
		// Pre-gather scene properties:
		uint total_tris = 0;
		// 1. Update BVH resources //
		{
			for (size_t i = 0; i < renderables.size(); ++i)
			{
				const GRenderableComponent& renderable = *renderables[i];

				if (renderable.geometry)
				{
					GGeometryComponent* geometry = renderable.geometry;
					assert(geometry);

					if (geometry->GetNumParts() == 0 || !geometry->HasRenderData())
					{
						continue;
					}

					const std::vector<Primitive>& parts = geometry->GetPrimitives();

					for (size_t i = 0; i < parts.size(); ++i)
					{
						const Primitive& prim = parts[i];
						GPrimBuffers* part_buffers = geometry->GetGPrimBuffer(i);
						if (prim.GetPrimitiveType() != GeometryComponent::PrimitiveType::TRIANGLES
							|| part_buffers == nullptr)
						{
							continue;
						}
						total_tris += (uint)prim.GetNumIndices() / 3;
					}
				}
			}

			// for all emitters.. TODO

			if (total_tris > 0 && !scene_bvh.primitiveCounterBuffer.IsValid())
			{
				GPUBufferDesc desc;
				desc.bind_flags = BindFlag::SHADER_RESOURCE;
				desc.stride = sizeof(uint);
				desc.size = desc.stride;
				desc.misc_flags = ResourceMiscFlag::BUFFER_RAW;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &scene_bvh.primitiveCounterBuffer);
				device->SetName(&scene_bvh.primitiveCounterBuffer, "GPUBVH::primitiveCounterBuffer");
			}

			if (total_tris == 0)
			{
				scene_bvh.primitiveCounterBuffer = {};
			}

			if (total_tris > scene_bvh.primitiveCapacity)
			{
				scene_bvh.primitiveCapacity = std::max(2u, total_tris);

				GPUBufferDesc desc;

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(BVHNode);
				desc.size = desc.stride * scene_bvh.primitiveCapacity * 2;
				desc.misc_flags = ResourceMiscFlag::BUFFER_RAW;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &scene_bvh.bvhNodeBuffer);
				device->SetName(&scene_bvh.bvhNodeBuffer, "GPUBVH::BVHNodeBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(uint);
				desc.size = desc.stride * scene_bvh.primitiveCapacity * 2;
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &scene_bvh.bvhParentBuffer);
				device->SetName(&scene_bvh.bvhParentBuffer, "GPUBVH::BVHParentBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(uint);
				desc.size = desc.stride * (((scene_bvh.primitiveCapacity - 1) + 31) / 32); // bitfield for internal nodes
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &scene_bvh.bvhFlagBuffer);
				device->SetName(&scene_bvh.bvhFlagBuffer, "GPUBVH::BVHFlagBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(uint);
				desc.size = desc.stride * scene_bvh.primitiveCapacity;
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &scene_bvh.primitiveIDBuffer);
				device->SetName(&scene_bvh.primitiveIDBuffer, "GPUBVH::primitiveIDBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(BVHPrimitive);
				desc.size = desc.stride * scene_bvh.primitiveCapacity;
				desc.misc_flags = ResourceMiscFlag::BUFFER_RAW;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &scene_bvh.primitiveBuffer);
				device->SetName(&scene_bvh.primitiveBuffer, "GPUBVH::primitiveBuffer");

				desc.bind_flags = BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
				desc.stride = sizeof(float); // morton buffer is float because sorting must be done and gpu sort operates on floats for now!
				desc.size = desc.stride * scene_bvh.primitiveCapacity;
				desc.misc_flags = ResourceMiscFlag::BUFFER_STRUCTURED;
				desc.usage = Usage::DEFAULT;
				device->CreateBuffer(&desc, nullptr, &scene_bvh.primitiveMortonBuffer);
				device->SetName(&scene_bvh.primitiveMortonBuffer, "GPUBVH::primitiveMortonBuffer");
			}
		}

		// 2. Build
		auto range = profiler::BeginRangeGPU("BVH Rebuild (Scene)", &cmd);
		{
			uint32_t primitive_count = 0; // this is supposed to be same to total_tris

			GPUResource unbind;
			const GPUResource* res_unbind[4] = { &unbind , &unbind , &unbind , &unbind };

			device->EventBegin("BVH - Primitive (Scene) Builder", cmd);
			device->BindComputeShader(&bvhPrimitives, cmd);
			const GPUResource* uavs[] = {
				&scene_bvh.primitiveIDBuffer,
				&scene_bvh.primitiveBuffer,
				&scene_bvh.primitiveMortonBuffer,
			};
			device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

			for (size_t i = 0; i < renderables.size(); ++i)
			{
				const GRenderableComponent& renderable = *renderables[i];

				if (renderable.geometry)
				{
					GGeometryComponent* geometry = renderable.geometry;
					assert(geometry);

					if (geometry->GetNumParts() == 0 || !geometry->HasRenderData())
					{
						continue;
					}

					const std::vector<Primitive>& parts = geometry->GetPrimitives();

					for (size_t part_index = 0; part_index < parts.size(); ++part_index)
					{
						const Primitive& prim = parts[part_index];
						GPrimBuffers* part_buffers = geometry->GetGPrimBuffer(part_index);
						if (prim.GetPrimitiveType() != GeometryComponent::PrimitiveType::TRIANGLES
							|| part_buffers == nullptr)
						{
							continue;
						}
						primitive_count += (uint)prim.GetNumIndices() / 3;

						BVHPushConstants push;
						push.geometryIndex = geometry->geometryOffset;
						push.subsetIndex = part_index;
						push.primitiveCount = primitive_count;
						push.instanceIndex = i; // same to renderable.renderableIndex
						assert(i == renderable.renderableIndex);
						push.vb_pos_w = -1; // not used
						push.ib = -1; // not used

						device->PushConstants(&push, sizeof(push), cmd);

						primitive_count += push.primitiveCount;

						device->Dispatch(
							(push.primitiveCount + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE,
							1,
							1,
							cmd
						);

						device->BindUAVs(res_unbind, 0, arraysize(res_unbind), cmd);
					}
				}
			}

			// for all emitters.. TODO

			GPUBarrier barriers[] = {
				GPUBarrier::Memory()
			};
			device->Barrier(barriers, arraysize(barriers), cmd);

			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&scene_bvh.primitiveCounterBuffer, ResourceState::SHADER_RESOURCE, ResourceState::COPY_DST),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}
			device->UpdateBuffer(&scene_bvh.primitiveCounterBuffer, &primitive_count, cmd);
			{
				GPUBarrier barriers[] = {
					GPUBarrier::Buffer(&scene_bvh.primitiveCounterBuffer, ResourceState::COPY_DST, ResourceState::SHADER_RESOURCE),
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}
			device->EventEnd(cmd);

			device->EventBegin("BVH - Sort Primitive Mortons", cmd);
			gpusortlib::Sort(primitive_count, gpusortlib::COMPARISON_FLOAT, scene_bvh.primitiveMortonBuffer, scene_bvh.primitiveCounterBuffer, 0, scene_bvh.primitiveIDBuffer, cmd);
			device->EventEnd(cmd);

			device->EventBegin("BVH - Build Hierarchy", cmd);
			{
				device->BindComputeShader(&bvhHierarchy, cmd);
				const GPUResource* uavs[] = {
					&scene_bvh.bvhNodeBuffer,
					&scene_bvh.bvhParentBuffer,
					&scene_bvh.bvhFlagBuffer
				};
				device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

				const GPUResource* res[] = {
					&scene_bvh.primitiveCounterBuffer,
					&scene_bvh.primitiveIDBuffer,
					&scene_bvh.primitiveMortonBuffer,
				};
				device->BindResources(res, 0, arraysize(res), cmd);

				device->Dispatch((primitive_count + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1, cmd);

				device->BindUAVs(res_unbind, 0, arraysize(res_unbind), cmd);
				device->BindResources(res_unbind, 0, arraysize(res_unbind), cmd);

				GPUBarrier barriers[] = {
					GPUBarrier::Memory()
				};
				device->Barrier(barriers, arraysize(barriers), cmd);
			}
			device->EventEnd(cmd);

			device->EventBegin("BVH - Propagate AABB", cmd);
			{
				GPUBarrier barriers[] = {
					GPUBarrier::Memory()
				};
				device->Barrier(barriers, arraysize(barriers), cmd);

				device->BindComputeShader(&bvhPropagateAABB, cmd);
				const GPUResource* uavs[] = {
					&scene_bvh.bvhNodeBuffer,
					&scene_bvh.bvhFlagBuffer,
				};
				device->BindUAVs(uavs, 0, arraysize(uavs), cmd);

				const GPUResource* res[] = {
					&scene_bvh.primitiveCounterBuffer,
					&scene_bvh.primitiveIDBuffer,
					&scene_bvh.primitiveBuffer,
					&scene_bvh.bvhParentBuffer,
				};
				device->BindResources(res, 0, arraysize(res), cmd);

				device->Dispatch((primitive_count + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1, cmd);

				device->BindUAVs(res_unbind, 0, arraysize(res_unbind), cmd);
				device->BindResources(res_unbind, 0, arraysize(res_unbind), cmd);

				device->Barrier(barriers, arraysize(barriers), cmd);
			}
			device->EventEnd(cmd);
		}
		profiler::EndRange(range); // BVH rebuild

#ifdef BVH_VALIDATE
		{
			GPUBufferDesc readback_desc;
			bool download_success;

			// Download primitive count:
			readback_desc = scene_bvh.primitiveCounterBuffer.GetDesc();
			readback_desc.usage = USAGE_STAGING;
			readback_desc.CPUAccessFlags = CPU_ACCESS_READ;
			readback_desc.bind_flags = 0;
			readback_desc.Flags = 0;
			GPUBuffer readback_primitiveCounterBuffer;
			device->CreateBuffer(&readback_desc, nullptr, &readback_primitiveCounterBuffer);
			uint primitiveCount;
			download_success = device->DownloadResource(&scene_bvh.primitiveCounterBuffer, &readback_primitiveCounterBuffer, &primitiveCount, cmd);
			assert(download_success);

			if (primitiveCount > 0)
			{
				const uint leafNodeOffset = primitiveCount - 1;

				// Validate node buffer:
				readback_desc = scene_bvh.bvhNodeBuffer.GetDesc();
				readback_desc.usage = USAGE_STAGING;
				readback_desc.CPUAccessFlags = CPU_ACCESS_READ;
				readback_desc.bind_flags = 0;
				readback_desc.Flags = 0;
				GPUBuffer readback_nodeBuffer;
				device->CreateBuffer(&readback_desc, nullptr, &readback_nodeBuffer);
				vector<BVHNode> nodes(readback_desc.size / sizeof(BVHNode));
				download_success = device->DownloadResource(&scene_bvh.bvhNodeBuffer, &readback_nodeBuffer, nodes.data(), cmd);
				assert(download_success);
				set<uint> visitedLeafs;
				vector<uint> stack;
				stack.push_back(0);
				while (!stack.empty())
				{
					uint nodeIndex = stack.back();
					stack.pop_back();

					if (nodeIndex >= leafNodeOffset)
					{
						// leaf node
						assert(visitedLeafs.count(nodeIndex) == 0); // leaf node was already visited, this must not happen!
						visitedLeafs.insert(nodeIndex);
					}
					else
					{
						// internal node
						BVHNode& node = nodes[nodeIndex];
						stack.push_back(node.LeftChildIndex);
						stack.push_back(node.RightChildIndex);
					}
				}
				for (uint i = 0; i < primitiveCount; ++i)
				{
					uint nodeIndex = leafNodeOffset + i;
					BVHNode& leaf = nodes[nodeIndex];
					assert(leaf.LeftChildIndex == 0 && leaf.RightChildIndex == 0); // a leaf must have no children
					assert(visitedLeafs.count(nodeIndex) > 0); // every leaf node must have been visited in the traversal above
				}

				// Validate flag buffer:
				readback_desc = scene_bvh.bvhFlagBuffer.GetDesc();
				readback_desc.usage = USAGE_STAGING;
				readback_desc.CPUAccessFlags = CPU_ACCESS_READ;
				readback_desc.bind_flags = 0;
				readback_desc.Flags = 0;
				GPUBuffer readback_flagBuffer;
				device->CreateBuffer(&readback_desc, nullptr, &readback_flagBuffer);
				vector<uint> flags(readback_desc.size / sizeof(uint));
				download_success = device->DownloadResource(&scene_bvh.bvhFlagBuffer, &readback_flagBuffer, flags.data(), cmd);
				assert(download_success);
				for (auto& x : flags)
				{
					if (x > 2)
					{
						assert(0); // flagbuffer anomaly detected: node can't have more than two children (AABB propagation step)!
						break;
					}
				}
			}
		}
#endif // BVH_VALIDATE
		return true;
	}
}