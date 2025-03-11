#ifndef SHADERINTEROP_DVR_H
#define SHADERINTEROP_DVR_H
#include "ShaderInterop.h"

static const uint DVR_BLOCKSIZE = VISIBILITY_BLOCKSIZE;
static const float FLT_OPACITY_MIN = 1.f / 255.f;		// trival storage problem 

#define SLICER_FLAG_ONLY_OUTLINE (1u << 0)

#define SLICER_OUTSIDE_PLANE 1 << 0
#define SLICER_DEPTH_OUTLINE 1 << 1
#define SLICER_DEPTH_OUTLINE_DIRTY 1 << 2
#define SLICER_SOLID_FILL_PLANE 1 << 3
#define SLICER_DIRTY 1 << 4
#define SLICER_DEBUG 1 << 5

// depending on rendering options
struct VolumePushConstants
{
	uint instanceIndex; // to get ShaderMeshInstance
	int sculptStep;
	// OTF
	float opacity_correction;
	float main_visible_min_sample; // TODO: will be packed

	uint inout_color_Index;
	uint inout_linear_depth_Index;

	uint target_otf_slot; 
	int bitmaskbuffer;
};

struct SlicerMeshPushConstants
{
	uint instanceIndex;
	uint materialIndex;
	int BVH_counter;
	int BVH_nodes;

	int BVH_primitives;
	uint sliceFlags;
	float outlineThickness; // in pixel
	uint padding0;
};

#endif // SHADERINTEROP_DVR_H
