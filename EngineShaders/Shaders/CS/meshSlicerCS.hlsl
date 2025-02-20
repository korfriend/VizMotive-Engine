#include "../Globals.hlsli"
#include "../CommonHF/objectRayHF.hlsli"
#define K_NUM 2
#include "../CommonHF/kbufferHF.hlsli"

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<uint> layer0_color : register(u1); // RGBA
RWTexture2D<uint> layer1_color : register(u2);
RWTexture2D<float> layer0_depth : register(u3);
RWTexture2D<float> layer1_depth : register(u4); // pixel-space distance_map
RWTexture2D<uint> layer0_thick_asum : register(u5); // RG
RWTexture2D<uint> layer1_thick_asum : register(u6);

inline bool InsideTest(const float3 originWS, const float3 originOS, const float3 rayDirOS, const float2 minMaxT, const float4x4 os2ws, inout float minDistOnPlane)
{
	RayDesc ray;
	ray.Origin = originOS;
	ray.Direction = rayDirOS;
	ray.TMin = 0;// minMaxT.x;
	ray.TMax = minMaxT.y;

	RayHit hit = TraceRay_Closest(ray);
	int hitTriIdx = hit.distance >= FLT_MAX - 1 ? -1 : hit.primitiveID.primitiveIndex;
	bool isInsideOnPlane = false;
	if (hitTriIdx >= 0) {
		bool localInside = hit.is_backface;
		isInsideOnPlane = localInside;

		float3 hit_os = ray.Origin + hit.distance * ray.Direction;
		float4 hit_ws = mul(os2ws, float4(hit_os, 1));
		hit_ws.xyz /= hit_ws.w;
		float dist_pixels = length(hit_ws.xyz - originWS) / push.pixelSize;

		if (minDistOnPlane * minDistOnPlane > dist_pixels * dist_pixels) {
			minDistOnPlane = localInside ? -dist_pixels : dist_pixels;
		}
	}
	return isInsideOnPlane;
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

	if (asuint(layer1_depth[pixel]) == WILDCARD_DEPTH_OUTLINE)
	{
		return;
	}

	uint v_layer1_thich_asum = layer1_thick_asum[pixel];
	if (v_layer1_thich_asum == WILDCARD_DEPTH_OUTLINE_DIRTY)
	{
		layer1_thick_asum[pixel] = 0;
		layer1_depth[pixel] = asfloat(WILDCARD_DEPTH_OUTLINE);
		return;
	}

	layer1_depth[pixel] = asfloat(OUTSIDE_PLANE);

	bool disabled_filling = push.sliceFlags & SLICER_FLAG_ONLY_OUTLINE;

	const float2 uv = ((float2)pixel + (float2)0.5) * camera.internal_resolution_rcp;
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
	float4x4 ws2os = inst.transformRaw_inv.GetMatrix();
	float4x4 os2ws = inst.transformRaw.GetMatrix();

	float sliceThickness = push.sliceThickness;
	if (sliceThickness > 0 && !disabled_filling)
	{
		// correction ray
		ray_ws.Origin -= ray_ws.Direction * sliceThickness * 0.5f;
	}
	else
	{
		sliceThickness = 0;
	}

	if (sliceThickness == 0)
	{
		// this is for rubust signed distance computation
		ray_tmin = 0;
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

	const float3x3 ws2os_adj = inst.transformRaw_inv.GetMatrixAdjoint();
	//const float3x3 ws2os_adj = (float3x3)inst.transformRaw_inv.GetMatrix();

	float3 ray_dir_os = normalize(mul(ws2os_adj, ray_ws.Direction));

	bool isInsideOnPlane = false;
	float minDistOnPlane = FLT_MAX; // for on-the-fly zeroset contouring


	float3 up_os = normalize(mul(ws2os_adj, camera.up));
	float3 right_os = normalize(cross(ray_dir_os, up_os));

	// safe inside test (up- and down-side)
	bool isInsideOnPlaneUp0 = InsideTest(ray_ws.Origin, origin_os.xyz, up_os, float2(ray_tmin, ray_tmax), os2ws, minDistOnPlane);
	bool isInsideOnPlaneUp1 = InsideTest(ray_ws.Origin, origin_os.xyz, -up_os, float2(ray_tmin, ray_tmax), os2ws, minDistOnPlane);

	// safe inside test (right- and left-side)
	bool isInsideOnPlaneRight0 = InsideTest(ray_ws.Origin, origin_os.xyz, right_os, float2(ray_tmin, ray_tmax), os2ws, minDistOnPlane);
	bool isInsideOnPlaneRight1 = InsideTest(ray_ws.Origin, origin_os.xyz, -right_os, float2(ray_tmin, ray_tmax), os2ws, minDistOnPlane);
	isInsideOnPlane = isInsideOnPlaneUp0 && isInsideOnPlaneUp1 && isInsideOnPlaneRight0 && isInsideOnPlaneRight1;

	if (sliceThickness == 0)
	{
		layer1_depth[pixel] = minDistOnPlane;
		// DO NOT finish here (for solid filling)
	}

	// forward check
	bool hit_on_forward_ray = false;
	float forward_hit_depth = FLT_MAX;
	bool is_front_forward_face = false;
	{
		RayDesc ray;
		ray.Origin = origin_os.xyz;
		ray.Direction = ray_dir_os;
		ray.TMin = ray_tmin;
		ray.TMax = ray_tmax;

		RayHit hit = TraceRay_Closest(ray);
		int hitTriIdx = hit.distance >= FLT_MAX - 1 ? -1 : hit.primitiveID.primitiveIndex;
		forward_hit_depth = hit.distance;
		is_front_forward_face = !hit.is_backface;
		hit_on_forward_ray = hitTriIdx >= 0;
	}

	if (!hit_on_forward_ray) {
		// note ... when ray passes through a triangle edge or vertex, hit may not be detected
		return;
	}

	if (!isInsideOnPlane && forward_hit_depth > sliceThickness_os)
	{
		return;
	}

	// backward check
	bool hit_on_backward_ray = false;
	float backward_hit_depth = FLT_MAX;	// os
	float last_layer_depth = -1.f; 
	bool is_front_backward_face = false;
	{
		float3 ray_backward_origin_os = origin_os.xyz + ray_dir_os * sliceThickness_os;

		RayDesc ray;
		ray.Origin = ray_backward_origin_os;
		ray.Direction = -ray_dir_os;
		ray.TMin = ray_tmin;
		ray.TMax = ray_tmax;

		RayHit hit = TraceRay_Closest(ray);
		int hitTriIdx = hit.distance >= FLT_MAX - 1 ? -1 : hit.primitiveID.primitiveIndex;
		backward_hit_depth = hit.distance;

		is_front_backward_face = !hit.is_backface;
		hit_on_backward_ray = hitTriIdx >= 0;
		if (hit_on_backward_ray) last_layer_depth = sliceThickness_os - backward_hit_depth;
	}

	float thickness_through_os = 0.f;
	[branch]
	if (sliceThickness > 0)
	{
		float3 forward_hit_pos_os = origin_os.xyz + ray_dir_os * forward_hit_depth;

		RayDesc ray;
		ray.Origin = forward_hit_pos_os;
		ray.Direction = ray_dir_os;
		ray.TMin = ray_tmin;
		ray.TMax = ray_tmax;

#define HITBUFFERSIZE 5
		bool is_backface_prev = !is_front_forward_face;
		uint hitCount = 0; // just for debugging

		if (last_layer_depth > 0)
		{
			if (is_backface_prev)
			{
				thickness_through_os = forward_hit_depth;
			}

			float ray_march_dist = forward_hit_depth;

			[loop]
			for (uint i = 0; i < HITBUFFERSIZE; i++)
			{
				RayHit hit = TraceRay_Closest(ray);
				int hitTriIdx = hit.distance >= FLT_MAX - 1 ? -1 : hit.primitiveID.primitiveIndex;
				float hitDistance = hit.distance;

				ray.Origin += ray.Direction * hit.distance;
				ray.TMin = ray_tmin; // small offset!
				ray.TMax = ray_tmax;

				if (hitTriIdx < 0)
				{
					break;
				}
				ray_march_dist += hitDistance;

#ifdef BVH_LEGACY
				bool localInside = dot(trinormal, test_raydir.xyz) > 0;
#else
				bool localInside = hit.is_backface;
#endif
				if (localInside)
				{
					if (ray_march_dist < sliceThickness_os)
					{
						thickness_through_os += hitDistance;
					}
					else
					{
						thickness_through_os += hitDistance - (ray_march_dist - sliceThickness_os);
					}
					hitCount++;
				}
				if (ray_march_dist >= sliceThickness_os)
				{
					break;
				}
				is_backface_prev = localInside;
			}
		}
		else if (!is_front_backward_face)
		{
			thickness_through_os = sliceThickness_os;
		}
	}

	float zdepth0 = -1.f, zdepth1 = -1.f; // WS
	if (isInsideOnPlane) {
		if (sliceThickness == 0) {

			if (!hit_on_backward_ray) {
				return;
			}

			zdepth0 = 0;
			zdepth1 = 0;
			thickness_through_os = 0;
		}
		else { // sliceThickness > 0

			zdepth0 = 0;

			if (!is_front_forward_face)
			{
				RayDesc ray;
				ray.Origin = origin_os.xyz;
				ray.Direction = -ray_dir_os;
				ray.TMin = ray_tmin;
				ray.TMax = ray_tmax;

				RayHit hit = TraceRay_Closest(ray);
				int hitTriIdx = hit.distance >= FLT_MAX - 1 ? -1 : hit.primitiveID.primitiveIndex;
				float backward_dist = hit.distance;
				bool localInside = hit.is_backface;

				if (hitTriIdx >= 0 && localInside)
				{
					float3 vray0_os = backward_dist * ray_dir_os;
					zdepth0 = -length(mul(vray0_os, (float3x3)os2ws));
				}
			}

			if (last_layer_depth > 0) 
			{
				float3 vray1_os = last_layer_depth * ray_dir_os;
				zdepth1 = length(mul(vray1_os, (float3x3)os2ws));
			}
			else
			{
				zdepth1 = sliceThickness;
			}
		}
	}
	else {
		if (sliceThickness == 0) 
		{
			return;
		}
		// outside
		float3 vray0_os = min(forward_hit_depth, sliceThickness_os) * ray_dir_os;
		zdepth0 = length(mul(vray0_os, (float3x3)os2ws));
		if (zdepth0 > sliceThickness) 
		{
			return;
		}

		if (last_layer_depth > 0) 
		{
			float3 vray1_os = last_layer_depth * ray_dir_os;
			zdepth1 = length(mul(vray1_os, (float3x3)os2ws));
		}
		else
		{
			zdepth1 = sliceThickness;
		}
	}

	ShaderMaterial material = GetMaterial();
	half4 base_color = material.GetBaseColor();

	if (sliceThickness == 0)
	{
		base_color.a = half(0.3f);
		half zthickness = half(0.1f);
		if (disabled_filling) 
		{
			base_color = half4(0.f, 0.f, 0.f, 0.01f);
			zthickness = half(0.f);
		}
		
		base_color.rgb *= base_color.a;
		
		Fragment frag;
		frag.SetColor(base_color); // current
		frag.z = zdepth0;
		frag.zthick = zthickness;
		frag.opacity_sum = base_color.a;

		uint color0_packed = layer0_color[pixel];
		if (color0_packed != 0)
		{
			Fragment fragPrev;
			fragPrev.color_packed = color0_packed;

			// Note: layer0_depth[pixel] can contain non-zero values inherited from previous thick-slicer.
			//	This case occurs during special outline-only passes defined in Actor configuration options.
			fragPrev.z = layer0_depth[pixel]; 
			fragPrev.Unpack_Zthick_AlphaSum(layer0_thick_asum[pixel]);
			
			half4 color_prev = fragPrev.GetColor();
			if (color_prev.a > half(0.01f))
				frag.SetColor(MixOpt(base_color, base_color.a, color_prev, fragPrev.opacity_sum));
			frag.opacity_sum += fragPrev.opacity_sum;
		}

		layer0_color[pixel] = frag.color_packed;
		layer0_depth[pixel] = zdepth0;
		layer0_thick_asum[pixel] = frag.Pack_Zthick_AlphaSum();
		
		if (push.sliceThickness == 0)
			inout_color[pixel] = (float4)frag.GetColor();

		layer1_thick_asum[pixel] = 0;
	}
	else
	{
		if (base_color.a < (half)0.01)
			return;
		// always to k-buf not render-out buffer
		half4 color_cur = base_color;

		// DOJO TO consider...
		// preserve the original alpha (i.e., v_rgba.a) or not..????
		color_cur.a *= (half)saturate(thickness_through_os / (sliceThickness_os) + 0.1f);
		if (color_cur.a < (half)0.01)
			return;
		//color_cur.a *= color_cur.a; // heuristic 
		color_cur.rgb *= color_cur.a;

		if (zdepth1 >= sliceThickness) // to avoid unexpected frustom culling!
		{
			zdepth1 = sliceThickness - 0.001f;
		}

		float vz_thickness = zdepth1 - zdepth0;

		Fragment frag;
		frag.SetColor(color_cur); // current
		frag.z = zdepth1;
		frag.zthick = (half)vz_thickness;
		frag.opacity_sum = color_cur.a;

		///Fill_kBuffer(pixel, 2, color_cur, zdepth1, vz_thickness);
		{
			uint color0_packed = layer0_color[pixel];
			if (color0_packed == 0)
			{
				layer0_color[pixel] = frag.color_packed;
				layer0_depth[pixel] = frag.z;
				layer0_thick_asum[pixel] = frag.Pack_Zthick_AlphaSum();

				//inout_color[pixel] = (float4)frag.GetColor(); // test //
			}
			else
			{
				uint color1_packed = layer1_color[pixel];
				uint frag_cnt = color1_packed == 0 ? 1 : 2;

				Fragment f_0;
				f_0.color_packed = color0_packed;
				f_0.z = layer0_depth[pixel];
				f_0.Unpack_Zthick_AlphaSum(layer0_thick_asum[pixel]);

				Fragment f_1;
				f_1.color_packed = color1_packed;
				f_1.z = layer1_depth[pixel];
				f_1.Unpack_Zthick_AlphaSum(v_layer1_thich_asum);
				Fragment fs[2] = { f_0, f_1 };

				Fill_kBuffer(frag, frag_cnt, fs);
			}
		}
	}
	return;
}