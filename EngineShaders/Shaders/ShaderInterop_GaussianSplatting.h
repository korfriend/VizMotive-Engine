#ifndef SHADERINTEROP_GS_H
#define SHADERINTEROP_GS_H
#include "ShaderInterop.h"

static const uint GSPLAT_TILESIZE = 16;
static const uint GSPLAT_GROUP_SIZE = GSPLAT_TILESIZE * GSPLAT_TILESIZE;

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

struct alignas(16) GaussianKernelAttribute {
	float4 conic_opacity;
	float4 color_radii;

	uint4 aabb; // bounding box 

	float2 uv; // pixel coords that is output of ndx2pix() func;
	float depth;
	uint magic;
};
static const uint GAUSSIANKERNELATTRIBUTE_OFFSET_CONIC_OPACITY = 0;
static const uint GAUSSIANKERNELATTRIBUTE_OFFSET_RGB_R = (4) * 4;
static const uint GAUSSIANKERNELATTRIBUTE_OFFSET_RADIUS = (7) * 4;
static const uint GAUSSIANKERNELATTRIBUTE_OFFSET_AABB = (8) * 4;
static const uint GAUSSIANKERNELATTRIBUTE_OFFSET_DEPTH = (12 + 2) * 4;
static const uint GAUSSIANKERNELATTRIBUTE_OFFSET_UV = (12) * 4;

struct GaussianPushConstants
{
	uint renderableIndex; // to get ShaderMeshInstance
	uint numGaussians;
	int geometryIndex;
	uint tileWidth;

	uint tileHeight;
	float focalX;
	float focalY;
	uint flags;
};

static const uint GSPLAT_FLAG_ANTIALIASING = 1u;

static const uint GAUSSIANCOUNTER_OFFSET_TOUCHCOUNT = 0;
static const uint GAUSSIANCOUNTER_OFFSET_OFFSETCOUNT = GAUSSIANCOUNTER_OFFSET_TOUCHCOUNT + 4;

#endif // SHADERINTEROP_GS_H
