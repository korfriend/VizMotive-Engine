#ifndef SHADERINTEROP_DVR_H
#define SHADERINTEROP_DVR_H
#include "ShaderInterop.h"

static const uint DVR_BLOCKSIZE = VISIBILITY_BLOCKSIZE;
static const FLT_OPACITY_MIN 1.f/255.f		// trival storage problem 

struct ClipBox
{
	float4x4 mat_clipbox_ws2bs; // To Clip Box Space (BS)
	float4 clip_plane;

	void GetCliPlane(out float3 pos, out float3 vec)
	{
		vec = normalize(clip_plane.xyz);
		pos = clip_plane.xyz * (-clip_plane.w) / dot(clip_plane.xyz, clip_plane.xyz);
	}
};

struct VolumePushConstants
{
	float4x4 mat_ws2ts;
	float4x4 mat_alignedvbox_ws2bs;

	// 1st bit : 0 (No) 1 (Clip Box)
	// 2nd bit : 0 (No) 1 (Clip plane)
	uint flags;
	float sample_dist; // WS unit
	float sample_range;
	float mask_sample_range;

	float main_visible_min_sample;
	int sculpt_index;
	float id2multiotf_convert;
	float opacity_correction;

	float3 volume_blocks_ts;

    ClipBox clip_box;
};

#endif // SHADERINTEROP_DVR_H
