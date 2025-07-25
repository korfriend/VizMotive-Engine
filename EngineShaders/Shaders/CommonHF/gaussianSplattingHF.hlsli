#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

struct GaussianSplattingInstance
{
	uint geometry_index;	// aliased from ShaderMeshInstance::alphaTest_size
	int gaussian_SHs_index;	// aliased from ShaderMeshInstance::geometryOffset
	int gaussian_scale_opacities_index; // aliased from ShaderMeshInstance::geometryCount
	int gaussian_quaternions_index; // aliased from ShaderMeshInstance::baseGeometryOffset

	int touchedTiles_0_index;	// aliased from ShaderMeshInstance::baseGeometryCount
	int offsetTiles_0_index;	// aliased from ShaderMeshInstance::meshletOffset
	int offsetTiles_Ping_index;	// aliased from ShaderMeshInstance::resLookupIndex
	int offsetTiles_Pong_index; // aliased from ShaderMeshInstance::emissive.x

	int sortKBufferEven_index;	// aliased from ShaderMeshInstance::emissive.y
	int sortKBufferOdd_index;	// aliased from ShaderMeshInstance::transformPrev.mat0.x 
	int sortVBufferEven_index;	// aliased from ShaderMeshInstance::transformPrev.mat0.y
	int sortVBufferOdd_index;	// aliased from ShaderMeshInstance::transformPrev.mat0.z

	int sortHistBuffer_index;	// aliased from ShaderMeshInstance::transformPrev.mat0.w
	int gaussian_vertex_attributes_index; // aliased from ShaderMeshInstance::transformPrev.mat1.x
	int totalSumBufferHost_index; // aliased from ShaderMeshInstance::transformPrev.mat1.y
	int tileBoundaryBuffer_index; // aliased from ShaderMeshInstance::transformPrev.mat1.z

	int duplicatedDepthGaussians_index; // aliased from ShaderMeshInstance::transformPrev.mat1.w, new version
	int readBackBufferTest_index;		// aliased from ShaderMeshInstance::transformPrev.mat2.x, readback buffer
	uint num_gaussians;	// aliased from aliased from ShaderMeshInstance::transformPrev.mat2.y
	
	// the remaining stuffs can be used as the same purpose of ShaderMeshInstance's attributes
	uint uid;
	uint flags;
	uint clipIndex;
	float3 aabbCenter;
	float aabbRadius;
	float fadeDistance;
	uint2 color;
	int lightmap;
	uint2 rimHighlight;

	void Init()
	{
		flags = 0u;
	}
	void Load(ShaderMeshInstance meshInst)
	{
		geometry_index = meshInst.alphaTest_size;
		gaussian_SHs_index = asint(meshInst.geometryOffset);
		gaussian_scale_opacities_index = asint(meshInst.geometryCount);
		gaussian_quaternions_index = asint(meshInst.baseGeometryOffset);
		touchedTiles_0_index = asint(meshInst.baseGeometryCount);
		offsetTiles_0_index = asint(meshInst.meshletOffset);
		offsetTiles_Ping_index = asint(meshInst.resLookupIndex);
		offsetTiles_Pong_index = asint(meshInst.emissive.x);
		sortKBufferEven_index = asint(meshInst.emissive.y);
		sortKBufferOdd_index = asint(meshInst.transformPrev.mat0.x);
		sortVBufferEven_index = asint(meshInst.transformPrev.mat0.y);
		sortVBufferOdd_index = asint(meshInst.transformPrev.mat0.z);
		sortHistBuffer_index = asint(meshInst.transformPrev.mat0.w);
		gaussian_vertex_attributes_index = asint(meshInst.transformPrev.mat1.x);
		totalSumBufferHost_index = asint(meshInst.transformPrev.mat1.y);
		tileBoundaryBuffer_index = asint(meshInst.transformPrev.mat1.z);
		duplicatedDepthGaussians_index = asint(meshInst.transformPrev.mat1.w);
		readBackBufferTest_index = asint(meshInst.transformPrev.mat2.x);
		num_gaussians = asuint(meshInst.transformPrev.mat2.y);

		// the same attrobutes to ShaderMeshInstance
		uid = meshInst.uid;
		flags = meshInst.flags;
		clipIndex = meshInst.clipIndex;
		aabbCenter = meshInst.center;
		aabbRadius = meshInst.radius;
		fadeDistance = meshInst.fadeDistance;
		color = meshInst.color;
		lightmap = meshInst.lightmap;
		rimHighlight = meshInst.rimHighlight;
	}
};