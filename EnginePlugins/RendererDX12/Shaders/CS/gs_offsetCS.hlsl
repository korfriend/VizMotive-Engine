#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"

PUSHCONSTANT(sortVars, GaussianSortConstants);

RWTexture2D<unorm float4> inout_color : register(u0);
RWStructuredBuffer<uint> pingBuffer : register(u3); // offsetTiles_ping
RWStructuredBuffer<uint> pongBuffer : register(u4); // offsetTiles_pong

[numthreads(256, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint timestep = sortVars.timestamp;
    uint index = dtid.x;
    
    uint offset = 1u << timestep;

    if (index < offset)
    {
        if ((timestep % 2) == 0)
        {
            // read ping -> write pong
            pongBuffer[index] = pingBuffer[index];
        }
        else
        {
            // read pong  -> write ping
            pingBuffer[index] = pongBuffer[index];
        }
    }
    else
    {
        // if index >= offset , accum value index-offset 
        if ((timestep % 2) == 0)
        {
            // read ping -> write pong
            pongBuffer[index] = pingBuffer[index] + pingBuffer[index - offset];
        }
        else
        {
            // read pong -> read ping
            pingBuffer[index] = pongBuffer[index] + pongBuffer[index - offset];
        }
    }
}
