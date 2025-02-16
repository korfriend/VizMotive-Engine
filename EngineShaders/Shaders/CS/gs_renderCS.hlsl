#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   
// test210
//PUSHCONSTANT(gaussians, GaussianPushConstants);
PUSHCONSTANT(totalSum, GaussianSortConstants);

// UINT2 (width, height) 를 받아야 함.

//Buffer<float4> gs_vertexAttr = bindless_buffers_float4[gaussiansSorts.gaussian_Vertex_Attributes_index];
// vertexAttrs consists of 16 elements (4 + 4 + 4 + 4 = 16)
// first 4 elements : (conic params + opacity)
// second 4 elements : color + radius
// third 4 elements : bounding box (aabb)
// fourth 4 elements : uv + depth + padding

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<unorm float4> prefixSum : register(u1);

StructuredBuffer<VertexAttribute> Vertices : register(t0);
StructuredBuffer<uint> offsetTiles : register(t1); // -> tileBoundaryBuffer
StructuredBuffer<uint> sortValueEven : register(t2); // -> sortVBufferEven

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
        if (sortValueEven[10000] == 0)
        {
            inout_color[pixel_coord] = float4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
        }
        else
        {
            inout_color[pixel_coord] = float4(0.0f, 1.0f, 1.0f, 1.0f); // Cyan
        }
    }
    
    //Buffer<float> totalSumPrefix = bindless_buffers_float4[gaussiansSorts.gaussian_Vertex_Attributes_index];
    //먼저 버퍼 인덱스를 불러와야함 -> push constant 에 index 만들기 
    
    //uint2 localID = DTid - Gid * GS_TILESIZE;

    //uint2 curr_uv = uint2(Gid.x * GS_TILESIZE + localID.x, Gid.y * GS_TILESIZE + localID.y);

    //if (curr_uv.x >= width || curr_uv.y >= height)
    //{
    //    return;
    //}

    //uint tiles_width = (width + GS_TILESIZE - 1) / GS_TILESIZE;

    //uint boundaryIndex = (Gid.x + Gid.y * tiles_width) * 2;
    //uint start = Boundaries[boundaryIndex];
    //uint end = Boundaries[boundaryIndex + 1];

    //float T = 1.0f;
    //float3 c = float3(0.0f, 0.0f, 0.0f);

    //for (uint i = start; i < end; i++)
    //{
    //    uint vertex_key = SortedVertices[i];
    //    VertexAttribute v = Vertices[vertex_key];

    //    float2 pixelPos = float2(curr_uv);
    //    float2 diff = v.uv - pixelPos;

    //    float power = -0.5f * (v.conic_opacity.x * diff.x * diff.x +
    //        v.conic_opacity.z * diff.y * diff.y)
    //        - v.conic_opacity.y * diff.x * diff.y;

    //    if (power > 0.0f)
    //    {
    //        continue;
    //    }

    //    float alpha = min(0.99f, v.conic_opacity.w * exp(power));
    //    if (alpha < (1.0f / 255.0f))
    //    {
    //        continue;
    //    }

    //    float test_T = T * (1.0f - alpha);
    //    if (test_T < 0.0001f)
    //    {
    //        break;
    //    }

    //    c += v.color_radii.xyz * alpha * T;
    //    T = test_T;
    //}

    //inout_color[curr_uv] = float4(c, 1.0f);
}
