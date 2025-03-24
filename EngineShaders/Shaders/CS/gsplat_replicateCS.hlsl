#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"
#include "../CommonHF/surfaceHF.hlsli"

PUSHCONSTANT(push, GaussianPushConstants);

RWStructuredBuffer<uint64_t> duplicatedGaussianKey : register(u1); // OutKeys
RWStructuredBuffer<uint> duplicatedGaussianValue : register(u2); // OutPayloads

ByteAddressBuffer rawGaussianKernelAttributes : register(t0);
//StructuredBuffer<GaussianKernelAttribute> gaussianKernelAttributes : register(t0);
StructuredBuffer<uint> offsetTiles : register(t1);

[numthreads(256, 1, 1)] // indirect? 256,1,1 is optimal?
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    uint index = DTid.x;
    if (index >= push.numGaussians)
        return;

    uint addr = index * sizeof(GaussianKernelAttribute);
    if (asfloat(rawGaussianKernelAttributes.Load(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_RADIUS)) == 0)
        return;
    //GaussianKernelAttribute v = gaussianKernelAttributes[index];
    uint depthBits = rawGaussianKernelAttributes.Load(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_DEPTH);
    float4 aabb = asfloat(rawGaussianKernelAttributes.Load4(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_AABB));
    
    //if (index >= prefixSum.Length())
    //    return;

    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;
    uint tileX = push.tileX;
    
    uint offset_index = offsetTiles[index]; // (index == 0) ? 0 : prefixSum[index - 1];

    for (uint i = (uint) aabb.x; i < (uint)aabb.z; i++)
    {
        for (uint j = (uint) aabb.y; j < (uint)aabb.w; j++)
        {
            uint64_t tileIndex = ((uint64_t) i) + ((uint64_t) j * tileX);
            //uint depthBits = asuint(depth);
            uint64_t k = (tileIndex << 32) | ((uint64_t) depthBits);
            duplicatedGaussianKey[offset_index] = k;
            duplicatedGaussianValue[offset_index] = index;
            offset_index++;
        }
    }
}
