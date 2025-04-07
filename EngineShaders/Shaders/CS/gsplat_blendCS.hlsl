#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

PUSHCONSTANT(push, GaussianPushConstants);

RWTexture2D<unorm float4> inout_color : register(u0);

ByteAddressBuffer rawGaussianKernelAttributes : register(t0);
StructuredBuffer<uint> tileRange : register(t1); // tile range
StructuredBuffer<uint> replicatedGaussianValue : register(t2);    // sorted replicated buffer
StructuredBuffer<uint> sortedIndices : register(t3);    // sorted replicated buffer

//StructuredBuffer<uint2> replicatedGaussianKey : register(t4);    // sorted replicated buffer

[numthreads(GSPLAT_TILESIZE, GSPLAT_TILESIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 GID : SV_GroupID)
{
    uint tileX = GID.x;
    uint tileY = GID.y;
    uint localX = GTid.x;
    uint localY = GTid.y;

    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;

    uint2 pixel = uint2(tileX * GSPLAT_TILESIZE + localX, tileY * GSPLAT_TILESIZE + localY);
    if (pixel.x >= W || pixel.y >= H)
    {
        return;
    }
    //inout_color[pixel] = float4(1, 1, 0, 1);
    //return;
    uint tile_index = (tileX + tileY * push.tileWidth) * 2;
    if (tileRange[tile_index] == ~0u)
    {
        //inout_color[pixel] = float4(1, 1, 0, 1);
        return;
    }
    uint sortedReplicate_indexStart = tileRange[tile_index];
    uint sortedReplicate_indexEnd = tileRange[tile_index + 1];
    //if (sortedReplicate_indexEnd - sortedReplicate_indexStart > 10000)
    //{
    //    //inout_color[pixel] = float4(0, 1, 0, 1);
    //    //return;
    //}

    float4 integrated_rgba = float4(0.0f, 0.0f, 0.0f, 0.0f);

    uint safe_count = 0;
    [loop]
    for (uint i = sortedReplicate_indexStart; i < sortedReplicate_indexEnd; i++)
    {
        if (safe_count++ > 10000)
            break;

        uint unsorted_replicate_index = sortedIndices[i];
        uint gaussian_index = replicatedGaussianValue[unsorted_replicate_index];
        uint addr = gaussian_index * sizeof(GaussianKernelAttribute);

        //GaussianKernelAttribute kernel = rawGaussianKernelAttributes.Load<GaussianKernelAttribute>(addr);

        //float2 gaussian_pixelUV = kernel.uv;
        float2 gaussian_pixelUV = asfloat(rawGaussianKernelAttributes.Load2(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_UV));
        float2 dist = gaussian_pixelUV - float2(pixel);
        //float4 co = kernel.conic_opacity; 
        float4 co = asfloat(rawGaussianKernelAttributes.Load4(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_CONIC_OPACITY));
        float power = -0.5f * (co.x * dist.x * dist.x + co.z * dist.y * dist.y) - co.y * dist.x * dist.y;
        if (power > 0.0f)
            continue;

        // Eq. (2) from 3D Gaussian splatting paper.
        // Obtain alpha by multiplying with Gaussian opacity
        // and its exponential falloff from mean.
        // Avoid numerical instabilities (see paper appendix). 
        float alpha = min(0.99f, co.w * exp(power));
        if (alpha < (1.0f / 255.0f))
            continue;

        //float4 rgb_radii = kernel.color_radii; 
        float4 rgb_radii = asfloat(rawGaussianKernelAttributes.Load4(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_RGB_R));

        integrated_rgba += 
            float4(rgb_radii.rgb * alpha, alpha) 
            * (1.f - integrated_rgba.a);
        if (integrated_rgba.a > 0.999f)
            break;
    }

    inout_color[pixel] = integrated_rgba;
}