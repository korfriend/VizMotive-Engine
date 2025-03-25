#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

PUSHCONSTANT(push, GaussianPushConstants);

RWTexture2D<unorm float4> inout_color : register(u0);

ByteAddressBuffer rawGaussianKernelAttributes : register(t0);
StructuredBuffer<uint> tileRange : register(t1); // tile range
StructuredBuffer<uint> replicatedGaussianValue : register(t2);    // sorted replicated buffer
StructuredBuffer<uint> sortedIndices : register(t3);    // sorted replicated buffer

[numthreads(GSPLAT_TILESIZE, GSPLAT_TILESIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 GID : SV_GroupID)
{
    uint tileX = GID.x;
    uint tileY = GID.y;
    uint localX = GTid.x;
    uint localY = GTid.y;

    //curr_uv ??
    uint2 pixel = uint2(tileX * GSPLAT_TILESIZE + localX, tileY * GSPLAT_TILESIZE + localY);
    if (pixel.x >= push.tileWidth || pixel.y >= push.tileHeight)
        return;

    uint tile_index = (tileX + tileY * push.tileWidth) * 2;
    uint sortedReplicate_indexStart = tileRange[tile_index];
    uint sortedReplicate_indexEnd = tileRange[tile_index + 1];

    float T = 1.0f;
    float3 c = float3(0.0f, 0.0f, 0.0f);

    for (uint i = sortedReplicate_indexStart; i < sortedReplicate_indexEnd; i++)
    {
        uint unsorted_replicate_index = sortedIndices[i];
        uint gaussian_index = replicatedGaussianValue[unsorted_replicate_index];
        uint addr = gaussian_index * sizeof(GaussianKernelAttribute);

        float2 gaussian_pixelUV = asfloat(rawGaussianKernelAttributes.Load2(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_UV));
        float2 dist = gaussian_pixelUV - float2(pixel);
        float4 co = asfloat(rawGaussianKernelAttributes.Load4(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_CONIC_OPACITY));
        float power = -0.5f * (co.x * dist.x * dist.x + co.z * dist.y * dist.y) - co.y * dist.x * dist.y;
        if (power > 0.0f)
            continue;

        float alpha = min(0.99f, co.w * exp(power));
        if (alpha < (1.0f / 255.0f))
            continue;

        float test_T = T * (1 - alpha);
        if (test_T < 0.0001f)
            break;

        float4 rgb_radii = asfloat(rawGaussianKernelAttributes.Load4(addr + GAUSSIANKERNELATTRIBUTE_OFFSET_RGB_R));
        c += rgb_radii.rgb * alpha * T;
        T = test_T;
    }

    inout_color[pixel] = float4(c, 1.0f);
    //inout_color[int2(curr_uv)] = float4(1.0f, 1.0f, 1.0f, 1.0f);
}