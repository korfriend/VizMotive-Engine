#include "../Globals.hlsli"
#include "../ShaderInterop.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"

#ifdef VIEW_MSAA
Texture2DMS<uint> input_primitiveID_1 : register(t0);
Texture2DMS<uint> input_primitiveID_2 : register(t1);
#else
Texture2D<uint> input_primitiveID_1 : register(t0);
Texture2D<uint> input_primitiveID_2 : register(t1);
#endif // VIEW_MSAA

groupshared uint local_bin_mask;
groupshared uint local_bin_execution_mask_0[SHADERTYPE_BIN_COUNT + 1];
groupshared uint local_bin_execution_mask_1[SHADERTYPE_BIN_COUNT + 1];

RWStructuredBuffer<ShaderTypeBin> output_bins : register(u0);
RWStructuredBuffer<ViewTile> output_binned_tiles : register(u1);

RWTexture2D<float> output_depth_mip0 : register(u3);
RWTexture2D<float> output_depth_mip1 : register(u4);
RWTexture2D<float> output_depth_mip2 : register(u5);
RWTexture2D<float> output_depth_mip3 : register(u6);
RWTexture2D<float> output_depth_mip4 : register(u7);

RWTexture2D<float> output_lineardepth_mip0 : register(u8);
RWTexture2D<float> output_lineardepth_mip1 : register(u9);
RWTexture2D<float> output_lineardepth_mip2 : register(u10);
RWTexture2D<float> output_lineardepth_mip3 : register(u11);
RWTexture2D<float> output_lineardepth_mip4 : register(u12);

#ifdef VIEW_MSAA
RWTexture2D<uint> output_primitiveID_1 : register(u13);
RWTexture2D<uint> output_primitiveID_2 : register(u14);
#endif // VIEW_MSAA

[numthreads(DVR_BLOCKSIZE, DVR_BLOCKSIZE, 1)]
void main(uint2 Gid : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    float4 vis_out = 0;
	float depth_out = FLT_MAX;

    // mesh layers ... from PrimitiveID map
    // volume layers ... from depth map
}