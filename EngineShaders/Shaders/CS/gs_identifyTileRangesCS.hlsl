#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   

PUSHCONSTANT(gaussians, GaussianPushConstants);

RWStructuredBuffer<uint> boundaries : register(u0);
StructuredBuffer<uint64_t> keys : register(t0);

[numthreads(256, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint index = DTid.x;
    uint numInstances = gaussians.num_gaussians * 4;
    if (index >= numInstances)
        return;

    uint key = (uint)(keys[index] >> 32);
    if (index == 0)
    {
        boundaries[key * 2] = index;
    }
    else
    {
        uint prevKey = (uint)(keys[index - 1] >> 32);
        if (key != prevKey)
        {
            boundaries[key * 2] = index;
            boundaries[prevKey * 2 + 1] = index;
        }
    }
    if (index == numInstances - 1)
    {
        boundaries[key * 2 + 1] = numInstances;
    }
}