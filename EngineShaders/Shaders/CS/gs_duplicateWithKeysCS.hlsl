#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   

PUSHCONSTANT(tiles, GaussianSortConstants);

RWStructuredBuffer<uint64_t> OutKeys : register(u0); // sortKBufferEven
RWStructuredBuffer<uint> OutPayloads : register(u1); // sortVBufferEven

StructuredBuffer<GaussianKernelAttribute> Vertices : register(t0);
StructuredBuffer<uint> prefixSum : register(t1);

[numthreads(256, 1, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;

    uint index = DTid.x;
    
    if (Vertices[index].color_radii.w == 0)
        return;
    
    if (index >= tiles.num_gaussians)
        return;
    
    GaussianKernelAttribute v = Vertices[index];

    //if (index >= prefixSum.Length())
    //    return;
    
    uint tileX = tiles.tileX;
    
    uint ind = (index == 0) ? 0 : prefixSum[index - 1];

    for (uint i = (uint) v.aabb.x; i < (uint) v.aabb.z; i++)
    {
        for (uint j = (uint) v.aabb.y; j < (uint) v.aabb.w; j++)
        {
            uint64_t tileIndex = ((uint64_t) i) + ((uint64_t) j * tileX);
            uint depthBits = asuint(v.depth);
            uint64_t k = (tileIndex << 32) | ((uint64_t) depthBits);
            OutKeys[ind] = k;
            OutPayloads[ind] = index;
            ind++;
        }
    }
}
