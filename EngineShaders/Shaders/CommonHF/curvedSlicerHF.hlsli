#include "../Globals.hlsli"

inline float3 LoadCurvePoint(int bufferAt, int curvePointsBufferIndex)
{
	ByteAddressBuffer curve_points_buffer = bindless_buffers[descriptor_index(curvePointsBufferIndex)];
	uint3 data = curve_points_buffer.Load3(bufferAt * 12);
	return float3(asfloat(data.x), asfloat(data.y), asfloat(data.z));
}

RayDesc CreateCurvedSlicerRay(const uint2 pixel)
{
	// here, COS refers to Curved_Object_Space
	// Exploit ShaderCamera's parameters to store slicer's meta information
	ShaderCamera camera = GetCamera();

	const float sliceThickness = camera.sliceThickness;
	const bool is_reverse_side = camera.options & SHADERCAMERA_OPTION_CURVED_SLICER_REVERSE_SIDE;
	const int curvePointsBufferIndex = camera.curvePointsBufferIndex;

	const int2 rt_resolution = int2(camera.internal_resolution.x, camera.internal_resolution.y);
	const float plane_width_pixel = camera.frustum_corners.cornersFAR[0].w;
	const float pitch = camera.frustum_corners.cornersFAR[1].w;
	const float plane_height_pixel = camera.frustum_corners.cornersFAR[2].w;

	const float3 pos_cos_TL = camera.frustum_corners.cornersFAR[0].xyz;
	const float3 pos_cos_TR = camera.frustum_corners.cornersFAR[1].xyz;
	const float3 pos_cos_BL = camera.frustum_corners.cornersFAR[2].xyz;
	const float3 pos_cos_BR = camera.frustum_corners.cornersFAR[3].xyz;

	const float3 plane_up = camera.up;
	const float plane_vertical_center_pixel = plane_height_pixel * 0.5f;
	const float thickness_position = 0.f; // 0 to thickness, here, we do not care

	const float plane_width_pixel_1 = plane_width_pixel - 1.f;
	const int plane_width_pixel_1_int = (int)plane_width_pixel_1;

	RayDesc ray;
	ray.Origin = (float3)0;
	ray.Direction = float3(0, 0, 1);
	ray.TMin = FLT_MAX;
	ray.TMax = FLT_MAX;

	float ratio_x1 = (float)(pixel.x) / (float)(rt_resolution.x - 1);
	float ratio_x0 = 1.f - ratio_x1;

	float3 pos_inter_top_cos;
	pos_inter_top_cos = ratio_x0 * pos_cos_TL + ratio_x1 * pos_cos_TR;
	if (pos_inter_top_cos.x < 0 || pos_inter_top_cos.x >= plane_width_pixel_1
		|| curvePointsBufferIndex < 0)
		return ray;

	float3 pos_inter_bottom_cos = ratio_x0 * pos_cos_BL + ratio_x1 * pos_cos_BR;

	float ratio_y1 = (float)(pixel.y) / (float)(rt_resolution.y - 1);
	float ratio_y0 = 1.f - ratio_y1;

	float3 pos_cos = ratio_y0 * pos_inter_top_cos + ratio_y1 * pos_inter_bottom_cos;

	if (pos_cos.y < 0 || pos_cos.y >= plane_height_pixel)
		return ray;


	int lookup_index = (int)floor(pos_inter_top_cos.x);
	float interpolate_ratio = pos_inter_top_cos.x - (float)lookup_index;

	int lookup_index0 = clamp(lookup_index + 0, 0, plane_width_pixel_1_int);
	int lookup_index1 = clamp(lookup_index + 1, 0, plane_width_pixel_1_int);
	int lookup_index2 = clamp(lookup_index + 2, 0, plane_width_pixel_1_int);


	float3 pos_ws_c0 = LoadCurvePoint(lookup_index0, curvePointsBufferIndex);
	float3 pos_ws_c1 = LoadCurvePoint(lookup_index1, curvePointsBufferIndex);
	float3 pos_ws_c2 = LoadCurvePoint(lookup_index2, curvePointsBufferIndex);

	float3 pos_ws_c_ = pos_ws_c0 * (1.f - interpolate_ratio) + pos_ws_c1 * interpolate_ratio;

	float3 tan_ws_c0 = normalize(pos_ws_c1 - pos_ws_c0);
	float3 tan_ws_c1 = normalize(pos_ws_c2 - pos_ws_c1);
	float3 tan_ws_c_ = normalize(tan_ws_c0 * (1.f - interpolate_ratio) + tan_ws_c1 * interpolate_ratio);
	float3 ray_dir_ws = normalize(cross(plane_up, tan_ws_c_));

	if (is_reverse_side)
		ray_dir_ws *= -1.f;

	float3 ray_origin_ws = pos_ws_c_ + ray_dir_ws * (thickness_position - sliceThickness * 0.5f);

	// start position //
	ray.Origin = ray_origin_ws + plane_up * pitch * (pos_cos.y - plane_vertical_center_pixel);
	ray.Direction = ray_dir_ws;
	ray.TMin = 0.0f;
	return ray;
}