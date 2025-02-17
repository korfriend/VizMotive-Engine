#include "../Globals.hlsli"
#include "../CommonHF/objectRayHF.hlsli"

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<uint> layer0_color : register(u1);
RWTexture2D<uint> layer1_color : register(u2);
RWTexture2D<float> layer0_depth : register(u3);
RWTexture2D<float> layer1_depth : register(u4);
RWTexture2D<float> layer0_thick : register(u5);
RWTexture2D<float> layer1_thick : register(u6);

// magic values
#define WILDCARD_DEPTH_OUTLINE 0x12345678
#define WILDCARD_DEPTH_OUTLINE_DIRTY 0x12345679
#define OUTSIDE_PLANE 0x87654321

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

	if (asuint(layer0_depth[pixel]) == WILDCARD_DEPTH_OUTLINE)
	{
		return;
	}

	if (asuint(layer1_depth[pixel]) == WILDCARD_DEPTH_OUTLINE_DIRTY)
	{
		layer0_depth[pixel] = asfloat(WILDCARD_DEPTH_OUTLINE);
		return;
	}
	
	layer0_depth[pixel] = asfloat(OUTSIDE_PLANE);

	bool disableSolidFill = push.sliceFlags & SLICER_FLAG_ONLY_OUTLINE;

	const float2 uv = ((float2)pixel + 0.5) * camera.internal_resolution_rcp;
	const float2 clipspace = uv_to_clipspace(uv);
	const float pixelSize = push.pixelSize;

	// IMPORTANT NOTE: Slicer assumes ORTHOGONAL PROJECTION!!!
#ifndef CURVEDPLANE
	RayDesc ray_ws = CreateCameraRay(clipspace);
#else
#endif

	ShaderClipper clipper; // TODO
	clipper.Init();

	float ray_tmin = 0.0001f; // MAGIC VALUE
	float ray_tmax = 1e20; 

	ShaderMeshInstance inst = load_instance(push.instanceIndex);
	float4x4 ws2os = (float4x4)1;// = inst.transformRaw_inv.GetMatrix();
	float4x4 os2ws = inst.transformRaw.GetMatrix();

	float sliceThickness = push.sliceThickness;
	if (sliceThickness > 0 && !disableSolidFill)
	{
		// correction ray
		ray_ws.Origin -= ray_ws.Direction * sliceThickness * 0.5f;
	}
	else
	{
		sliceThickness = 0;
	}

	float4 origin_os = mul(ws2os, float4(ray_ws.Origin, 1));
	origin_os.xyz /= origin_os.w;

	float sliceThickness_os = 0;
	if (sliceThickness > 0)
	{
		float3 end_ws = ray_ws.Origin + ray_ws.Direction * sliceThickness;
		float4 end_os = mul(ws2os, float4(end_ws, 1));
		end_os.xyz /= end_os.w;
		sliceThickness_os = length(end_os.xyz - origin_os.xyz);
	}

	const float3x3 ws2os_adj = (float4x4)1;//inst.transformRaw_inv.GetMatrixAdjoint();

	float3 ray_dir_os = normalize(mul(ws2os_adj, ray_ws.Direction));

	if (origin_os.z == 0) origin_os.z = 0.0001234f; // trick... for avoiding zero block skipping error
	if (origin_os.y == 0) origin_os.y = 0.0001234f; // trick... for avoiding zero block skipping error
	if (origin_os.x == 0) origin_os.x = 0.0001234f; // trick... for avoiding zero block skipping error
	if (ray_dir_os.z == 0) ray_dir_os.z = 0.0001234f; // trick... for avoiding zero block skipping error
	if (ray_dir_os.y == 0) ray_dir_os.y = 0.0001234f; // trick... for avoiding zero block skipping error
	if (ray_dir_os.x == 0) ray_dir_os.x = 0.0001234f; // trick... for avoiding zero block skipping error

	bool isInsideOnPlane = false;
	float minDistOnPlane = FLT_MAX; // for on-the-fly zeroset contouring


	float3 up_os = normalize(mul(ws2os_adj, camera.up));
	float3 right_os = normalize(cross(ray_dir_os, up_os));

	// safe inside test (up- and down-side)//
	{
		RayDesc ray;
		ray.Origin = origin_os.xyz;
		ray.Direction = up_os;
		ray.TMin = ray_tmin;
		ray.TMax = ray_tmax;

		RayHit hit = TraceRay_Closest(ray);
		int hitTriIdx = hit.distance >= FLT_MAX - 1 ? -1 : hit.primitiveID.primitiveIndex;

		if (hitTriIdx >= 0) {
			bool localInside = hit.is_backface;
			isInsideOnPlane = localInside;

			float3 hit_os = ray.Origin + hit.distance * ray.Direction;
			float4 hit_ws = mul(os2ws, float4(hit_os, 1));
			hit_ws.xyz /= hit_ws.w;
			float dist_pixels = length(hit_ws.xyz - ray_ws.Origin) / pixelSize;

			if (minDistOnPlane * minDistOnPlane > dist_pixels * dist_pixels) {
				minDistOnPlane = localInside ? -dist_pixels : dist_pixels;
			}
		}
	}





	ShaderMaterial material = GetMaterial();

	// TODO
	
    inout_color[pixel] = float4(1, 1, 0, 1);
}