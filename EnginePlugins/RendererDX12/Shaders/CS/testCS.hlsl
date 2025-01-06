#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"

PUSHCONSTANT(gaussians, GaussianPushConstants);

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<float> inout_linear_depth : register(u1);

[numthreads(1, 1, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{	
	const uint2 pixel = DTid.xy;

    ShaderCamera camera = GetCamera();
	//const bool pixel_valid = (pixel.x < camera.internal_resolution.x / 2) && (pixel.y < camera.internal_resolution.y /2);
    //if (!pixel_valid)
    //{
    //    return;
    //}

    // ply... position, normal...
    ShaderMeshInstance gs_instance = load_instance(gaussians.instanceIndex);
	uint subsetIndex = gaussians.geometryIndex - gs_instance.geometryOffset;
    ShaderGeometry geometry = load_geometry(subsetIndex);
    //if (geometry.vb_pos_w < 0)
    //{
    //    inout_color[pixel] = float4(1, 1, 0, 1);
    //    return;
    //}
	Buffer<float4> vb_pos_w = bindless_buffers_float4[geometry.vb_pos_w];
	float3 pos = vb_pos_w[0].xyz;

    //inout_color[pixel] = float4(pos.x - 0.580303, pos.y + 3.68339, pos.z + 3.44946, 1);
}