#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"

PUSHCONSTANT(sortVars, GaussianSortConstants);

RWTexture2D<unorm float4> inout_color : register(u0); // for debugging

// SRV
StructuredBuffer<uint> pingBufferSRV : register(t0);
StructuredBuffer<uint> pongBufferSRV : register(t1);

// UAV
RWStructuredBuffer<uint> pingBufferUAV : register(u3);
RWStructuredBuffer<uint> pongBufferUAV : register(u4);

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
            // read ping SRV -> write pong UAV
            pongBufferUAV[index] = pingBufferSRV[index];
        }
        else
        {
            // read pong SRV -> write ping UAV
            pingBufferUAV[index] = pongBufferSRV[index];
        }
    }
    else
    {
        if ((timestep % 2) == 0)
        {
            pongBufferUAV[index] = pingBufferSRV[index] + pingBufferSRV[index - offset];
        }
        else
        {
            pingBufferUAV[index] = pongBufferSRV[index] + pongBufferSRV[index - offset];
        }
    }
}