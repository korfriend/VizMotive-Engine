#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

RWStructuredBuffer<uint> tileRange : register(u0);

StructuredBuffer<uint2> replicatedGaussianKey : register(t0);
StructuredBuffer<uint> sortedIndices : register(t1);    // sorted replicated buffer
ByteAddressBuffer counterBuffer : register(t2);

[numthreads(GSPLAT_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint index = DTid.x;
    uint countReplcateGaussians = counterBuffer.Load(0);
    if (index >= countReplcateGaussians)
        return;

    uint unsorted_index = sortedIndices[index];
    uint2 replicateKey = replicatedGaussianKey[unsorted_index];
    uint key = replicateKey.x; // tile_id
    if (index == 0)
    {
        tileRange[key * 2] = index;
    }
    else
    {
        uint unsorted_index_prev = sortedIndices[index - 1];
        uint2 replicateKey_prev = replicatedGaussianKey[unsorted_index_prev];
        uint key_prev = replicateKey_prev.x;
        if (key != key_prev)
        {
            tileRange[key * 2] = index;
            tileRange[key_prev * 2 + 1] = index;
        }
    }

    if (index == countReplcateGaussians - 1)
    {
        tileRange[key * 2 + 1] = countReplcateGaussians;
    }
}