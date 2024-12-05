#ifndef SHADERINTEROP_DVR_H
#define SHADERINTEROP_DVR_H
#include "ShaderInterop.h"

static const uint DVR_BLOCKSIZE = VISIBILITY_BLOCKSIZE;
static const float FLT_OPACITY_MIN = 1.f/255.f;		// trival storage problem 

#define BitCheck(BITS, IDX) (BITS & (0x1u << IDX))

struct VolumePushConstants
{
	uint instanceIndex; // to get ShaderMeshInstance
	int sculptStep;

	// OTF
	float opacity_correction;
	float main_visible_min_sample;

	float3 singleblock_size_ts;
	float mask_value_range;

	float value_range;
	float mask_unormid_otf_map;
	int bitmaskbuffer;
	uint padding2;
};

#endif // SHADERINTEROP_DVR_H
