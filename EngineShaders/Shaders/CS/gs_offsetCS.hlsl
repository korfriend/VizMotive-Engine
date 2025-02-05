#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"

RWTexture2D<unorm float4> inout_color : register(u0);
RWStructuredBuffer<uint> src : register(u0); // touchedTiles_0
RWStructuredBuffer<uint> dst : register(u1); // offsetTiles_0

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint2 index = DTid.xy;
    inout_color[index] = float4(1.0f, 1.0f, 1.0f, 1.0f);
}
