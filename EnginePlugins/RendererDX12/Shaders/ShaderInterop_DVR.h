#ifndef SHADERINTEROP_DVR_H
#define SHADERINTEROP_DVR_H
#include "ShaderInterop.h"

static const uint DVR_BLOCKSIZE = VISIBILITY_BLOCKSIZE;
static const float FLT_OPACITY_MIN = 1.f/255.f;		// trival storage problem 

#define BitCheck(BITS, IDX) (BITS & (0x1u << IDX))

struct ClipBox
{
	float4x4 mat_clipbox_ws2bs; // To Clip Box Space (BS)
	float4 clip_plane;

#ifndef __cplusplus
	void GetCliPlane(out float3 pos, out float3 vec)
	{
		vec = normalize(clip_plane.xyz);
		pos = clip_plane.xyz * (-clip_plane.w) / dot(clip_plane.xyz, clip_plane.xyz);
	}
#endif
};

// VolumePushConstants::flags
static const uint APPLY_CLIPBOX = 0u;
static const uint APPLY_CLIPPLANE = 1u;
static const uint APPLY_JITTERING = 2u; 

struct VolumePushConstants
{
	float4x4 mat_ws2ts;
	float4x4 mat_alignedvbox_ws2bs;

	uint flags;
	float sample_dist; // WS unit
	float sample_range;
	float mask_sample_range;

	float main_visible_min_sample;
	int sculpt_index;
	float id2multiotf_convert;
	float opacity_correction;

	float3 singleblock_size_ts;

    ClipBox clip_box;
};

#endif // SHADERINTEROP_DVR_H
