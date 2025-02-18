#include "../Globals.hlsli"
#include "../ShaderInterop_BVH.h"

// This shader builds scene triangle data and performs BVH classification:
//	- This shader is run per object subset.
//	- Each thread processes a triangle
//	- Computes triangle bounding box, morton code and other properties and stores into global primitive buffer

PUSHCONSTANT(push, BVHPushConstants);

RWStructuredBuffer<uint> primitiveIDBuffer : register(u0);
RWByteAddressBuffer primitiveBuffer : register(u1);
RWStructuredBuffer<float> primitiveMortonBuffer : register(u2); // morton buffer is float because sorting is written for floats!

[numthreads(BVH_BUILDER_GROUPSIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
	if (DTid.x >= push.primitiveCount)
		return;

	PrimitiveID prim;
	prim.primitiveIndex = DTid.x;
	prim.instanceIndex = push.instanceIndex;
	prim.subsetIndex = push.subsetIndex;

	// IMPORTANT NOTE: ~GEOMETRYSPACE mode requires GetScene(), which means
	//	UpdateRenderData (uploading FrameCB) must be called prior to BVH generation

	int vb_pos_w = -1;
	int ib = -1;
#ifdef GEOMETRYSPACE
	vb_pos_w = push.vb_pos_w;
	ib = push.ib;
#else	// WORLD SPACE
	ShaderMeshInstance inst = load_instance(prim.instanceIndex);
	// push.geometryIndex is supposed to be same to inst.geometryOffset 
	ShaderGeometry geometry = load_geometry(inst.geometryOffset + prim.subsetIndex);
    ShaderInstanceResLookup instResLookup = load_instResLookup(inst.resLookupIndex + prim.subsetIndex);
	ShaderMaterial material = load_material(instResLookup.materialIndex);

	vb_pos_w = geometry.vb_pos_w;
	ib = geometry.ib;
#endif

	// we do NOT use indexOffset
	//uint startIndex = prim.primitiveIndex * 3 + geometry.indexOffset;
	uint startIndex = prim.primitiveIndex * 3;
	uint i0 = bindless_buffers_uint[descriptor_index(ib)][startIndex + 0];
	uint i1 = bindless_buffers_uint[descriptor_index(ib)][startIndex + 1];
	uint i2 = bindless_buffers_uint[descriptor_index(ib)][startIndex + 2];
	
#ifdef GEOMETRYSPACE
	float3 P0 = bindless_buffers_float4[descriptor_index(vb_pos_w)][i0].xyz;
	float3 P1 = bindless_buffers_float4[descriptor_index(vb_pos_w)][i1].xyz;
	float3 P2 = bindless_buffers_float4[descriptor_index(vb_pos_w)][i2].xyz;
#else
	float3 p_0 = bindless_buffers_float4[descriptor_index(vb_pos_w)][i0].xyz;
	float3 p_1 = bindless_buffers_float4[descriptor_index(vb_pos_w)][i1].xyz;
	float3 p_2 = bindless_buffers_float4[descriptor_index(vb_pos_w)][i2].xyz;
	float4 P0 = mul(inst.transform.GetMatrix(), float4(p_0, 1));
	float4 P1 = mul(inst.transform.GetMatrix(), float4(p_1, 1));
	float4 P2 = mul(inst.transform.GetMatrix(), float4(p_2, 1));
	P0.xyz /= P0.w;
	P1.xyz /= P1.w;
	P2.xyz /= P2.w;
#endif

	BVHPrimitive bvhprim;
	bvhprim.packed_prim = prim.pack2();
#ifdef GEOMETRYSPACE
	bvhprim.flags = ~0u;
#else
	bvhprim.flags = 0u;
	bvhprim.flags |= inst.layerMask & 0xFF;
	if (geometry.flags & SHADERMESH_FLAG_DOUBLE_SIDED)
	{
		bvhprim.flags |= BVH_PRIMITIVE_FLAG_DOUBLE_SIDED;
	}
	if (material.IsDoubleSided())
	{
		bvhprim.flags |= BVH_PRIMITIVE_FLAG_DOUBLE_SIDED;
	}
	if (material.IsTransparent() || material.GetAlphaTest() > 0)
	{
		bvhprim.flags |= BVH_PRIMITIVE_FLAG_TRANSPARENT;
	}
#endif
	bvhprim.x0 = P0.x;
	bvhprim.y0 = P0.y;
	bvhprim.z0 = P0.z;
	bvhprim.x1 = P1.x;
	bvhprim.y1 = P1.y;
	bvhprim.z1 = P1.z;
	bvhprim.x2 = P2.x;
	bvhprim.y2 = P2.y;
	bvhprim.z2 = P2.z;

	uint primitiveID = prim.primitiveIndex;
#ifdef __PSSL__
	primitiveBuffer.TypedStore<BVHPrimitive>(primitiveID * sizeof(BVHPrimitive), bvhprim);
#else
	primitiveBuffer.Store<BVHPrimitive>(primitiveID * sizeof(BVHPrimitive), bvhprim);
#endif // __PSSL__
	primitiveIDBuffer[primitiveID] = primitiveID; // will be sorted by morton so we need this!

	// Compute triangle morton code:
	float3 minAABB = min(P0.xyz, min(P1.xyz, P2.xyz));
	float3 maxAABB = max(P0.xyz, max(P1.xyz, P2.xyz));
	float3 centerAABB = (minAABB + maxAABB) * 0.5f;

#ifdef GEOMETRYSPACE
	const uint mortoncode = morton3D((centerAABB - push.aabb_min) * push.aabb_extents_rcp);
#else
	const uint mortoncode = morton3D((centerAABB - GetScene().aabb_min) * GetScene().aabb_extents_rcp);
#endif
	primitiveMortonBuffer[primitiveID] = (float)mortoncode; // convert to float before sorting

}
