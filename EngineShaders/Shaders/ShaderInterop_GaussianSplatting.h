#ifndef SHADERINTEROP_GS_H
#define SHADERINTEROP_GS_H
#include "ShaderInterop.h"

static const uint GSPLAT_TILESIZE = 16;

static const float SH_C0 = 0.28209479177387814f;
static const float SH_C1 = 0.4886025119029199f;
static const float SH_C2[5] = {
	1.0925484305920792f,
	-1.0925484305920792f,
	0.31539156525252005f,
	-1.0925484305920792f,
	0.5462742152960396f
};
static const float SH_C3[7] = {
	-0.5900435899266435f,
	2.890611442640554f,
	-0.4570457994644658f,
	0.3731763325901154f,
	-0.4570457994644658f,
	1.445305721320277f,
	-0.5900435899266435f
};

struct GaussianKernelAttribute {
	float4 conic_opacity;
	float4 color_radii;
	uint4 aabb; // bounding box 
	float2 uv; // pixel coords that is output of ndx2pix() func;
	float depth;
	uint magic;
};

struct GaussianPushConstants
{
	uint instanceIndex; // to get ShaderMeshInstance
	uint tileX;
	uint tileY;
	uint timestamp;

	// for radix sort updates
	uint num_elements;
	uint shift;
	uint num_workgroups;
	uint num_blocks_per_workgroup;

	int geometryIndex;
	uint padding0;
	uint padding1;
	uint padding2;
};

static const uint GAUSSIANCOUNTER_OFFSET_TOUCHCOUNT = 0;
static const uint GAUSSIANCOUNTER_OFFSET_OFFSETCOUNT = GAUSSIANCOUNTER_OFFSET_TOUCHCOUNT + 4;

#endif // SHADERINTEROP_GS_H
