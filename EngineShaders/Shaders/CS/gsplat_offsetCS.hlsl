#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

PUSHCONSTANT(push, GaussianPushConstants);

RWByteAddressBuffer counterBuffer : register(u10); // fixed

RWTexture2D<unorm float4> inout_color : register(u0); // for debugging

// SRV
StructuredBuffer<uint> touchedTiles : register(t0);

// UAV
RWStructuredBuffer<uint> offsetTiles : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint index = dtid.x;

    uint num_touched = touchedTiles[index];
    uint offset;
    counterBuffer.InterlockedAdd(GAUSSIANCOUNTER_OFFSET_TOUCHCOUNT, num_touched, offset);
    offsetTiles[index] = offset;
}