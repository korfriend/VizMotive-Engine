#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   

PUSHCONSTANT(totalSum, GaussianSortConstants);

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<unorm float4> prefixSum : register(u1);

StructuredBuffer<VertexAttribute> Vertices : register(t0);
StructuredBuffer<uint> offsetTiles : register(t1); // -> tileBoundaryBuffer
StructuredBuffer<uint> sortVBufferEven : register(t2); // -> sortVBufferEven
StructuredBuffer<uint> tileBoundaryBuffer : register(t3); // -> tileBoundaryBuffer


[numthreads(256, 1, 1)] // 16 x 16 x 1
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;

    uint idx = DTid.x;
    //if (idx >= gaussians.num_gaussians)
    //    return;

    Buffer<uint> totalPrefixSum = bindless_buffers_uint[totalSum.totalSumBufferHost_index];
    uint totalSum = totalPrefixSum[0];

    VertexAttribute v = Vertices[idx];

    int2 pixel_coord = int2(v.uv);

    if (pixel_coord.x >= 0 && pixel_coord.x < int(W) && pixel_coord.y >= 0 && pixel_coord.y < int(H))
    {
        //if (v.color_radii.w >= 4)
        if (tileBoundaryBuffer[11] == 0)
        {
            inout_color[pixel_coord] = float4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
        }
        else
        {
            inout_color[pixel_coord] = float4(0.0f, 1.0f, 1.0f, 1.0f); // Cyan
        }
    }
}
