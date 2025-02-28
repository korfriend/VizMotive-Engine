#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"

PUSHCONSTANT(sortConstants, GaussianSortConstants);

RWStructuredBuffer<uint64_t> OutKeys : register(u0); // sortKBufferEven
RWStructuredBuffer<uint> OutPayloads : register(u1); // sortVBufferEven

StructuredBuffer<VertexAttribute> Vertices : register(t0);
StructuredBuffer<uint> prefixSum : register(t1);


[numthreads(256, 1, 1)] void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    uint index = DTid.x;
    if (index >= sortConstants.num_gaussians * 4)
        return;

    VertexAttribute v = Vertices[index];
    if (v.color_radii.w == 0)
        return;

    uint tileX = sortConstants.tileX;

    uint ind = (index == 0) ? 0 : prefixSum[index - 1];

    uint aabbX = (uint)v.aabb.x;
    uint aabbY = (uint)v.aabb.y;
    uint aabbZ = (uint)v.aabb.z;
    uint aabbW = (uint)v.aabb.w;

    for (uint i = aabbX; i < aabbZ; i++)
    {
        for (uint j = aabbY; j < aabbW; j++)


        {
            uint64_t tileIndex = ((uint64_t)i) + ((uint64_t)j * tileX);
            uint depthBits = asuint(v.depth);
            uint64_t k = (tileIndex << 32) | ((uint64_t)depthBits);
            OutKeys[ind] = k;
            OutPayloads[ind] = index;
            ind++;
        }
    }
}
