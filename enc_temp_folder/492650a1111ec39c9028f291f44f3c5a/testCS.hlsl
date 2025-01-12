// Minimal HLSL snippet for atomic add and condition check (oldValue == 0 && newValue == 1).

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
    float p_w = 1.0f / (p_hom.w + 1e-7f);
    float3 p_proj = float3(p_hom.x * p_w, p_hom.y * p_w, p_hom.z * p_w);

    // Convert to screen coordinates
    float2 point_image = float2(
        ((p_proj.x + 1.0f) * W - 1.0f) * 0.5f,
        ((p_proj.y + 1.0f) * H - 1.0f) * 0.5f
    );

    // Clamp pixel
    uint2 point_pixel = uint2(
        clamp((uint) point_image.x, 0u, W - 1),
        clamp((uint) point_image.y, 0u, H - 1)
    );

    // Check both oldValue and the updated value
    if (oldValue == 0 && newValue == 0)
    {
        inout_color[point_pixel] = float4(1, 1, 1, 1); // White
    }
    else
    {
        inout_color[point_pixel] = float4(1, 1, 0, 1); // Yellow
    }
}
