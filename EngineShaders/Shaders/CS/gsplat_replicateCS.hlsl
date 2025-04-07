#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

PUSHCONSTANT(push, GaussianPushConstants);

RWStructuredBuffer<uint2> replicatedGaussianKey : register(u0); 
RWStructuredBuffer<uint> replicatedGaussianValue : register(u1);
RWStructuredBuffer<uint> unsortedIndices : register(u2);

ByteAddressBuffer rawGaussianKernelAttributes : register(t0);
StructuredBuffer<uint> offsetTiles : register(t1);

[numthreads(GSPLAT_GROUP_SIZE, 1, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    uint index = DTid.x;
    if (index >= push.numGaussians)
        return;

    uint offset_index = offsetTiles[index];
    if (offset_index == ~0u)
    {
        replicatedGaussianKey[offset_index] = uint2(~0u, ~0u);
        return;
    }

    uint addr = index * sizeof(GaussianKernelAttribute);

    if (asfloat(rawGaussianKernelAttributes.Load(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_RADIUS)) == 0)
        return;

    uint depthBits = rawGaussianKernelAttributes.Load(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_DEPTH);
    uint4 aabb = (rawGaussianKernelAttributes.Load4(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_AABB));

    ShaderCamera camera = GetCamera();
    //uint W = camera.internal_resolution.x;
    //uint H = camera.internal_resolution.y;
    uint tileWidth = push.tileWidth;
    
    for (uint i = (uint) aabb.x; i < (uint)aabb.z; i++)
    {
        for (uint j = (uint) aabb.y; j < (uint)aabb.w; j++)
        {
            uint tileIndex = i + j * tileWidth;
            replicatedGaussianKey[offset_index] = uint2(tileIndex, depthBits);
            replicatedGaussianValue[offset_index] = index;

            unsortedIndices[offset_index] = offset_index;
            offset_index++;
        }
    }
}
