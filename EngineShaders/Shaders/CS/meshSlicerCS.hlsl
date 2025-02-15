#include "../Globals.hlsli"
//#include "../CommonHF/objectHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<uint> layer0_color : register(u1);
RWTexture2D<uint> layer1_color : register(u2);
RWTexture2D<float> layer0_depth : register(u3);
RWTexture2D<float> layer1_depth : register(u4);
RWTexture2D<float> layer0_thick : register(u5);
RWTexture2D<float> layer1_thick : register(u6);

PUSHCONSTANT(push, MeshPushConstants);

inline ShaderGeometry GetMesh()
{
	return load_geometry(push.geometryIndex);
}
inline ShaderMaterial GetMaterial()
{
	return load_material(push.materialIndex);
}

[numthreads(VISIBILITY_BLOCKSIZE, VISIBILITY_BLOCKSIZE, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{	
    ShaderCamera camera = GetCamera();

	const uint2 pixel = DTid.xy;
	const bool pixel_valid = (pixel.x < camera.internal_resolution.x) && (pixel.y < camera.internal_resolution.y);
    if (!pixel_valid)
    {
        return;
    }

	const float2 uv = ((float2)pixel + 0.5) * camera.internal_resolution_rcp;
	const float2 clipspace = uv_to_clipspace(uv);
	RayDesc ray = CreateCameraRay(clipspace);

	// push
	ShaderClipper clipper; // TODO
	clipper.Init();

	ShaderMaterial material = GetMaterial();

	// TODO
	
    inout_color[pixel] = float4(1, 1, 0, 1);
}