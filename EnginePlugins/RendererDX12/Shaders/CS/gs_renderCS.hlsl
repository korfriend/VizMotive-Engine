#include "../Globals.hlsli"
#include "../ShaderInterop_GS.hlsli"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   

struct VertexAttribute
{
    float2 uv;
    float4 conic_opacity;   // xyz : conic params, w : opacity 
    float4 color_radii;     // xyz : color, w: radius 
};

StructuredBuffer<VertexAttribute> Vertices : register(t0);
StructuredBuffer<uint> Boundaries : register(t1);
StructuredBuffer<uint> SortedVertices : register(t2);

RWTexture2D<unorm float4> inout_color : register(u0);

[numthreads(GS_TILESIZE, GS_TILESIZE, 1)] // 16 x 16 x 1
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    ShaderCamera camera = GetCamera();
    uint width = camera.internal_resolution.x;
    uint height = camera.internal_resolution.y;

    uint2 localID = DTid - Gid * GS_TILESIZE;

    uint2 curr_uv = uint2(Gid.x * GS_TILESIZE + localID.x, Gid.y * GS_TILESIZE + localID.y);

    if (curr_uv.x >= width || curr_uv.y >= height)
    {
        return;
    }

    uint tiles_width = (width + GS_TILESIZE - 1) / GS_TILESIZE;

    uint boundaryIndex = (Gid.x + Gid.y * tiles_width) * 2;
    uint start = Boundaries[boundaryIndex];
    uint end = Boundaries[boundaryIndex + 1];

    float T = 1.0f;
    float3 c = float3(0.0f, 0.0f, 0.0f);

    for (uint i = start; i < end; i++)
    {
        uint vertex_key = SortedVertices[i];
        VertexAttribute v = Vertices[vertex_key];

        float2 pixelPos = float2(curr_uv);
        float2 diff = v.uv - pixelPos;

        float power = -0.5f * (v.conic_opacity.x * diff.x * diff.x +
            v.conic_opacity.z * diff.y * diff.y)
            - v.conic_opacity.y * diff.x * diff.y;

        if (power > 0.0f)
        {
            continue;
        }

        float alpha = min(0.99f, v.conic_opacity.w * exp(power));
        if (alpha < (1.0f / 255.0f))
        {
            continue;
        }

        float test_T = T * (1.0f - alpha);
        if (test_T < 0.0001f)
        {
            break;
        }

        c += v.color_radii.xyz * alpha * T;
        T = test_T;
    }

    inout_color[curr_uv] = float4(c, 1.0f);
}
