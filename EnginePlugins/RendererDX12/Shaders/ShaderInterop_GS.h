#ifndef SHADERINTEROP_GS_H
#define SHADERINTEROP_GS_H
#include "ShaderInterop.h"

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
	//int duplicatedTileDepthGaussians_0_index;
	int duplicatedIdGaussians_index;

	uint num_gaussians;
	uint geometryIndex;
	uint materialIndex;
};

#endif // SHADERINTEROP_GS_H
