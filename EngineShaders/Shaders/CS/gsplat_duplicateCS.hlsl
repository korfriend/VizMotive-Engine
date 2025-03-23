#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"
#include "../CommonHF/surfaceHF.hlsli"

PUSHCONSTANT(push, GaussianPushConstants);

RWStructuredBuffer<uint64_t> duplicatedGaussianKey : register(u1); // OutKeys
RWStructuredBuffer<uint> duplicatedGaussianValue : register(u2); // OutPayloads

StructuredBuffer<GaussianKernelAttribute> gaussianKernelAttributes : register(t0);
StructuredBuffer<uint> offsetTiles : register(t1);

[numthreads(256, 1, 1)] // indirect? 256,1,1 is optimal?
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;

    uint index = DTid.x;
    
    if (gaussianKernelAttributes[index].color_radii.w == 0)
        return;
    
    if (index >= push.num_elements)
        return;
    
    GaussianKernelAttribute v = gaussianKernelAttributes[index];

    //if (index >= prefixSum.Length())
    //    return;
    
    uint tileX = push.tileX;
    
    uint ind = offsetTiles[index]; // (index == 0) ? 0 : prefixSum[index - 1];

    for (uint i = (uint) v.aabb.x; i < (uint) v.aabb.z; i++)
    {
        for (uint j = (uint) v.aabb.y; j < (uint) v.aabb.w; j++)
        {
            uint64_t tileIndex = ((uint64_t) i) + ((uint64_t) j * tileX);
            uint depthBits = asuint(v.depth);
            uint64_t k = (tileIndex << 32) | ((uint64_t) depthBits);
            duplicatedGaussianKey[ind] = k;
            duplicatedGaussianValue[ind] = index;
            ind++;
        }
    }
}
