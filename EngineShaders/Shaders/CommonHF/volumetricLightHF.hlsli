#ifndef VOLUMETRICLIGHT_HF
#define VOLUMETRICLIGHT_HF
#include "../globals.hlsli"
#include "brdf.hlsli"
#include "lightingHF.hlsli"

struct VertexToPixel {
	float4 pos			: SV_POSITION;
	float4 pos2D		: POSITION2D;
};

#endif // VOLUMETRICLIGHT_HF
