#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

ByteAddressBuffer counterBuffer : register(t0);
RWByteAddressBuffer indirectBuffers : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	// retrieve GPU itemcount:
	uint itemCount = counterBuffer.Load(0);

	if (itemCount > 0)
	{
		// calculate threadcount:
		uint threadCount = (itemCount / GSPLAT_GROUP_SIZE) + 1u;

		// and prepare to dispatch the sort for the alive simulated particles:
		indirectBuffers.Store3(0, uint3(threadCount, 1, 1));
	}
	else
	{
		// dispatch nothing:
		indirectBuffers.Store3(0, uint3(0, 0, 0));
	}
}