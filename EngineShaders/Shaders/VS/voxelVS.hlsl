#include "../globals.hlsli"
#include "../CommonHF/cube.hlsli"
#include "../CommonHF/voxelHF.hlsli"

uint main(uint vertexID : SV_VERTEXID) : VERTEXID
{
	return vertexID;
}
