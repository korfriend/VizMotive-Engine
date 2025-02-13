#ifndef SHADERINTEROP_GS_H
#define SHADERINTEROP_GS_H
#include "ShaderInterop.h"

#define TILE_WIDTH 16
#define TILE_HEIGHT 16

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

struct VertexAttribute {
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
	int gaussian_SHs_index;
	int gaussian_scale_opacities_index;
	int gaussian_quaternions_index;

	int touchedTiles_0_index;
	int offsetTiles_0_index;
	int offsetTiles_Ping_index; // ping buffer
	int offsetTiles_Pong_index; // ping buffer

	int duplicatedDepthGaussians_index; // new version
	int duplicatedIdGaussians_index;
	uint num_gaussians;
	uint geometryIndex;
	//uint materialIndex;
};

struct GaussianSortConstants
{
	int sortKBufferEven_index;
	int sortKBufferOdd_index;
	int sortVBufferEven_index;
	int sortVBufferOdd_index;

	int sortHistBuffer_index;
	int gaussian_vertex_attributes_index; // test210
	int totalSumBufferHost_index;
	int tileBoundaryBuffer_index;

	uint tileX;
	uint tileY;
	uint timestamp;
	uint num_gaussians;
};

struct GaussianRadixConstants
{
	uint g_num_elements;
	uint g_shift;
	uint g_num_workgroups;
	uint g_num_blocks_per_workgroup;
};


#endif // SHADERINTEROP_GS_H
