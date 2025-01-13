#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"

PUSHCONSTANT(gaussians, GaussianPushConstants);

RWTexture2D<unorm float4> inout_color : register(u0);
RWStructuredBuffer<uint> touchedTiles_0 : register(u1);

[numthreads(256, 1, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    // Global thread index along X-axis
    uint idx = DTid.x;

    // Exit if out of range
    if (idx >= gaussians.num_gaussians)
        return;

    // Atomically add 1 and retrieve the previous value
    uint oldValue;
    InterlockedAdd(touchedTiles_0[idx], 1, oldValue);

    // Read the updated value
    uint newValue = touchedTiles_0[idx];

    // Load camera data
    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;

    // Load instance and geometry
    ShaderMeshInstance gs_instance = load_instance(gaussians.instanceIndex);
    uint subsetIndex = gaussians.geometryIndex - gs_instance.geometryOffset;
    ShaderGeometry geometry = load_geometry(subsetIndex);

    // Fetch position
    Buffer<float4> vb_pos_w = bindless_buffers_float4[geometry.vb_pos_w];
    float3 pos = vb_pos_w[idx].xyz;

    // Transform to clip space
    float4 p_view = mul(float4(pos, 1.0f), camera.view);
    float4 p_hom = mul(p_view, camera.projection);

    // Avoid division by zero
    float p_w = 1.0f / max(p_hom.w, 1e-7f);

    // Convert to Normalized Device Coordinates (NDC)
    float3 p_proj = float3(p_hom.x * p_w, p_hom.y * p_w, p_hom.z * p_w);

    // Convert NDC (-1~1) to screen coordinates (0~W, 0~H)
    float2 point_image = float2(
        (p_proj.x * 0.5f + 0.5f) * (float)W,
        (p_proj.y * 0.5f + 0.5f) * (float)H
    );

    // Round to nearest pixel
    int2 point_pixel = int2(point_image + 0.5f);

    // If out of screen, do nothing
    if (point_pixel.x < 0 || point_pixel.y < 0 || point_pixel.x >= int(W) || point_pixel.y >= int(H))
        return;

    // Check both oldValue and the updated value
    if (oldValue == 0 && newValue == 1)
    {
        inout_color[point_pixel] = float4(1, 1, 1, 1); // White
    }
    else
    {
        inout_color[point_pixel] = float4(1, 1, 0, 1); // Yellow
    }
}
