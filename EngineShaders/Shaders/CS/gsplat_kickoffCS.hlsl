// HLSL Shader for Gaussian Screen-Space Covariance and Radius Computation
// Includes: Covariance Matrix Inversion (EWA Algorithm), Eigenvalues, and Bounding Rectangle Calculation

#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

RWByteAddressBuffer counterBuffer : register(u10); // fixed

[numthreads(1, 1, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
	counterBuffer.Store(GAUSSIANCOUNTER_OFFSET_TOUCHCOUNT, 0);
	counterBuffer.Store(GAUSSIANCOUNTER_OFFSET_OFFSETCOUNT, 0);
}