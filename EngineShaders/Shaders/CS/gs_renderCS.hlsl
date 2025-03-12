#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   
// test210
//PUSHCONSTANT(gaussians, GaussianPushConstants);
PUSHCONSTANT(totalSum, GaussianSortConstants);

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<unorm float4> prefixSum : register(u1);

StructuredBuffer<GaussianKernelAttribute> Vertices : register(t0);
StructuredBuffer<uint> offsetTiles : register(t1);
StructuredBuffer<uint> sortVBufferEven : register(t2);
StructuredBuffer<uint> touchedTiles_0 : register(t3);

[numthreads(256, 1, 1)] // 16 x 16 x 1
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;

    uint idx = DTid.x;
    
    // if (idx >= numGaussians) return;

    Buffer<uint> totalPrefixSum = bindless_buffers_uint[totalSum.totalSumBufferHost_index];
    uint totalSum = totalPrefixSum[0];
    
    GaussianKernelAttribute v = Vertices[idx];
    
    inout_color[v.uv] = float4(0.0f, 1.0f, 0.0f, 1.0f); // green
//    uint prefixValue = offsetTiles[idx]; // prefix sum 값
//// 전체 값(totalSum) 대비 비율로 정규화 (예: 0~1 사이의 값)
//    float normalized = (float) prefixValue / (float) totalSum;
//// 예를 들어, normalized 값에 따라 그라데이션을 표현
//    inout_color[uint2(v.uv)] = float4(normalized, 0.0, 1.0 - normalized, 1.0);

    
    //static const uint BLOCK_SIZE = 16;

    //uint2 rect_min = uint2(v.aabb.x, v.aabb.y);
    //uint2 rect_max = uint2(v.aabb.z, v.aabb.w);

    //uint2 pixel_rect_min = rect_min * BLOCK_SIZE;
    //uint2 pixel_rect_max = rect_max * BLOCK_SIZE;

    //pixel_rect_min.x = min(pixel_rect_min.x, W);
    //pixel_rect_min.y = min(pixel_rect_min.y, H);
    //pixel_rect_max.x = min(pixel_rect_max.x, W);
    //pixel_rect_max.y = min(pixel_rect_max.y, H);
    
    //for (uint py = pixel_rect_min.y; py < pixel_rect_max.y; py++)
    //{
    //    for (uint px = pixel_rect_min.x; px < pixel_rect_max.x; px++)
    //    {
    //        if (px == pixel_rect_min.x || px == pixel_rect_max.x - 1 ||
    //            py == pixel_rect_min.y || py == pixel_rect_max.y - 1)
    //        {
    //            inout_color[uint2(px, py)] = float4(0.0f, 1.0f, 0.0f, 1.0f); // green
    //        }
    //    }
    //}
    
}