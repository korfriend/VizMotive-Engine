#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   

#ifndef TILE_WIDTH
#define TILE_WIDTH 16
#endif

#ifndef TILE_HEIGHT
#define TILE_HEIGHT 16
#endif


PUSHCONSTANT(gaussian_sort, GaussianSortConstants);

RWTexture2D<unorm float4> inout_color : register(u0);

StructuredBuffer<VertexAttribute> attr : register(t0);
StructuredBuffer<uint> boundaries      : register(t1);
StructuredBuffer<uint> sorted_vertices : register(t2);


// compute shader의 스레드 그룹 크기 지정
[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID,   // 전역 스레드 ID (gl_GlobalInvocationID)
    uint3 GTid : SV_GroupThreadID,        // 그룹 내 스레드 ID (gl_LocalInvocationID)
    uint3 GId : SV_GroupID)              // 워크그룹 ID (gl_WorkGroupID)
{
    uint tileX = GId.x;
    uint tileY = GId.y;
    uint localX = GTid.x;
    uint localY = GTid.y;

    uint width = gaussian_sort.tileX;
    uint height = gaussian_sort.tileY;

    uint2 curr_uv = uint2(tileX * TILE_WIDTH + localX,
        tileY * TILE_HEIGHT + localY);

    if (curr_uv.x >= width || curr_uv.y >= height)
        return;

    uint tiles_width = (width + TILE_WIDTH - 1) / TILE_WIDTH;

    uint index = (tileX + tileY * tiles_width) * 2;
    uint start = boundaries[index];
    uint end = boundaries[index + 1];

    float T = 1.0f;
    float3 c = float3(0.0f, 0.0f, 0.0f);
    uint localIndex = localX + localY * TILE_WIDTH;

    for (uint i = start; i < end; i++)
    {
        uint vertex_key = sorted_vertices[i];
        float2 uv = attr[vertex_key].uv;
        float2 dist = uv - float2(curr_uv);
        float4 co = attr[vertex_key].conic_opacity;
        float power = -0.5f * (co.x * dist.x * dist.x + co.z * dist.y * dist.y)
            - co.y * dist.x * dist.y;

        if (power > 0.0f)
            continue;

        float alpha = min(0.99f, co.w * exp(power));
        if (alpha < (1.0f / 255.0f))
            continue;

        float test_T = T * (1.0f - alpha);
        if (test_T < 0.0001f)
            break;

        c += attr[vertex_key].color_radii.xyz * alpha * T;
        T = test_T;
    }

    // 최종 결과를 출력 텍스처에 기록 (알파값 1.0f)
    inout_color[curr_uv] = float4(c, 1.0f);
}
