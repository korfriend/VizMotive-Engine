#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

RWStructuredBuffer<uint> tileRange : register(u0);

StructuredBuffer<uint2> replicatedGaussianKey : register(t0);
ByteAddressBuffer counterBuffer : register(t1);

[numthreads(GSPLAT_GROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint index = DTid.x;
    uint countReplcateGaussians = counterBuffer.Load(0);
    if (index >= countReplcateGaussians)
        return;

    uint2 replicateKey = replicatedGaussianKey[index];
    uint key = replicateKey.x;
    if (index == 0)
    {
        tileRange[key * 2] = index;
    }
    else
    {
        uint2 replicateKey_prev = replicatedGaussianKey[index - 1];
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