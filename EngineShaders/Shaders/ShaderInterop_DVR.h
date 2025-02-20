#ifndef SHADERINTEROP_DVR_H
#define SHADERINTEROP_DVR_H
#include "ShaderInterop.h"

static const uint DVR_BLOCKSIZE = VISIBILITY_BLOCKSIZE;
static const float FLT_OPACITY_MIN = 1.f/255.f;		// trival storage problem 

#define SLICER_FLAG_ONLY_OUTLINE 1u << 0

struct VolumePushConstants
{
	uint instanceIndex; // to get ShaderMeshInstance
	int sculptStep;
	// OTF
	float opacity_correction;
	float main_visible_min_sample; // TODO: will be packed

	uint inout_color_Index;
	uint inout_linear_depth_Index;

	float mask_value_range; // TODO: will be packed
	float mask_unormid_otf_map;
};

struct SlicerMeshPushConstants
{
	uint instanceIndex;
	uint materialIndex;
	int BVH_counter;
	int BVH_nodes;

	int BVH_primitives;
	uint sliceFlags;
	float sliceThickness;
	float pixelSize; // NOTE: Slicer assumes ORTHOGONAL PROJECTION

	float outlineThickness; // in pixel
	uint padding0;
	uint padding1;
	uint padding2;
};

#endif // SHADERINTEROP_DVR_H
