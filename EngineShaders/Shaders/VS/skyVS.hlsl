#include "../Globals.hlsli"
#include "../CommonHF/icosphere.hlsli"
#include "../CommonHF/skyHF.hlsli"

struct VSOut
{
	float4 pos : SV_POSITION;
	float2 clipspace : TEXCOORD;
};

VSOut main(uint vI : SV_VERTEXID)
{
	VSOut Out;

	vertexID_create_fullscreen_triangle(vI, Out.pos);

	Out.clipspace = Out.pos.xy;

	return Out;
}
