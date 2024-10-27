#include "../Globals.hlsli"

PUSHCONSTANT(push, DebugObjectPushConstants);

float4 main(uint vertexID : SV_VertexID) : SV_POSITION
{
	return mul(g_xTransform, float4(bindless_buffers_float4[push.vbPosW][vertexID].xyz, 1));
}