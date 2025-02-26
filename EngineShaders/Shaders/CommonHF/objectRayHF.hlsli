#include "../Globals.hlsli"
#include "../ShaderInterop_BVH.h"
#include "../ShaderInterop_DVR.h"

PUSHCONSTANT(push, SlicerMeshPushConstants);

#define primitiveCounterBuffer bindless_buffers[descriptor_index(push.BVH_counter)]
#define bvhNodeBuffer bindless_buffers[descriptor_index(push.BVH_nodes)]
#define primitiveBuffer bindless_buffers[descriptor_index(push.BVH_primitives)]

// magic values
#define WILDCARD_DEPTH_OUTLINE 1 << 1
#define WILDCARD_DEPTH_OUTLINE_DIRTY 1 << 2
#define OUTSIDE_PLANE 1

inline ShaderMaterial GetMaterial()
{
	return load_material(push.materialIndex);
}

inline RayDesc CreateCameraRay(float2 clipspace)
{
	float4 unprojectedNEAR = mul(GetCamera().inverse_view_projection, float4(clipspace, 1, 1));
	unprojectedNEAR.xyz /= unprojectedNEAR.w;
	float4 unprojectedFAR = mul(GetCamera().inverse_view_projection, float4(clipspace, 0, 1));
	unprojectedFAR.xyz /= unprojectedFAR.w;

	RayDesc ray;
	ray.Origin = unprojectedNEAR.xyz;
	ray.Direction = normalize(unprojectedFAR.xyz - ray.Origin);
	ray.TMin = 0.001;
	ray.TMax = FLT_MAX;

	return ray;
}

struct RayHit
{
	float2 bary;
	float distance;
	PrimitiveID primitiveID;
	bool is_backface;
};

inline RayHit CreateRayHit()
{
	RayHit hit;
	hit.bary = 0;
	hit.distance = FLT_MAX;
	hit.is_backface = false;
	//hit.primitiveID.init();
	return hit;
}

#ifndef RAYTRACE_STACKSIZE
#define RAYTRACE_STACKSIZE 64
#endif // RAYTRACE_STACKSIZE

inline void IntersectTriangle(
	in RayDesc ray,
	inout RayHit bestHit,
	in BVHPrimitive prim
)
{
	float3 v0v1 = prim.v1() - prim.v0();
	float3 v0v2 = prim.v2() - prim.v0();
	float3 pvec = cross(ray.Direction, v0v2);
	float det = dot(v0v1, pvec);

	// ray and triangle are parallel if det is close to 0
	if (abs(det) < 1e-6f)
		return;
	float invDet = rcp(det);

	float3 tvec = ray.Origin - prim.v0();
	float u = dot(tvec, pvec) * invDet;
	if (u < 0 || u > 1)
		return;

	float3 qvec = cross(tvec, v0v1);
	float v = dot(ray.Direction, qvec) * invDet;
	if (v < 0 || u + v > 1)
		return;

	float t = dot(v0v2, qvec) * invDet;

	if (t >= ray.TMin && t <= bestHit.distance)
	{
		RayHit hit;
		hit.distance = t;
		hit.primitiveID = prim.primitiveID();
		hit.bary = float2(u, v);
		hit.is_backface = det < 0;

		bestHit = hit;
	}
}

static const float eps = 1e-6;

inline bool IntersectNode(
	in RayDesc ray,
	in BVHNode box,
	in float3 rcpDirection,
	in float primitive_best_distance
)
{
	const float t0 = (box.min.x - ray.Origin.x) * rcpDirection.x;
	const float t1 = (box.max.x - ray.Origin.x) * rcpDirection.x;
	const float t2 = (box.min.y - ray.Origin.y) * rcpDirection.y;
	const float t3 = (box.max.y - ray.Origin.y) * rcpDirection.y;
	const float t4 = (box.min.z - ray.Origin.z) * rcpDirection.z;
	const float t5 = (box.max.z - ray.Origin.z) * rcpDirection.z;
	const float tmin = max(max(min(t0, t1), min(t2, t3)), min(t4, t5)); // close intersection point's distance on ray
	const float tmax = min(min(max(t0, t1), max(t2, t3)), max(t4, t5)); // far intersection point's distance on ray

	return (tmax < -eps || tmin > tmax + eps || tmin > primitive_best_distance) ? false : true;
	//if (tmax < 0 || tmin > tmax || tmin > primitive_best_distance) // this also checks if a better primitive was already hit or not and skips if yes
	//{
	//	return false;
	//}
	//else
	//{
	//	return true;
	//}
}

inline bool IntersectNode(
	in RayDesc ray,
	in BVHNode box,
	in float3 rcpDirection
)
{
	const float t0 = (box.min.x - ray.Origin.x) * rcpDirection.x;
	const float t1 = (box.max.x - ray.Origin.x) * rcpDirection.x;
	const float t2 = (box.min.y - ray.Origin.y) * rcpDirection.y;
	const float t3 = (box.max.y - ray.Origin.y) * rcpDirection.y;
	const float t4 = (box.min.z - ray.Origin.z) * rcpDirection.z;
	const float t5 = (box.max.z - ray.Origin.z) * rcpDirection.z;
	const float tmin = max(max(min(t0, t1), min(t2, t3)), min(t4, t5)); // close intersection point's distance on ray
	const float tmax = min(min(max(t0, t1), max(t2, t3)), max(t4, t5)); // far intersection point's distance on ray

	//return (tmax < 0 || tmin > tmax) ? false : true;
	return (tmax < -eps || tmin > tmax + eps) ? false : true;
}

// Returns the closest hit primitive if any (useful for generic trace). If nothing was hit, then rayHit.distance will be equal to FLT_MAX
inline RayHit TraceRay_Closest(RayDesc ray, uint groupIndex = 0)
{
	const float3 rcpDirection = rcp(ray.Direction);
	
	RayHit bestHit = CreateRayHit();

#ifndef RAYTRACE_STACK_SHARED
	// Emulated stack for tree traversal:
	uint stack[RAYTRACE_STACKSIZE][1];
#endif // RAYTRACE_STACK_SHARED
	uint stackpos = 0;

	const uint primitiveCount = primitiveCounterBuffer.Load(0);
	const uint leafNodeOffset = primitiveCount - 1;

	// push root node
	stack[stackpos++][groupIndex] = 0;

	[loop]
	while (stackpos > 0) {
		// pop untraversed node
		const uint nodeIndex = stack[--stackpos][groupIndex];

		BVHNode node = bvhNodeBuffer.Load<BVHNode>(nodeIndex * sizeof(BVHNode));
		//BVHNode node = bvhNodeBuffer[nodeIndex];

		if (IntersectNode(ray, node, rcpDirection, bestHit.distance))
		{
			if (nodeIndex >= leafNodeOffset)
			{
				// Leaf node
				const uint primitiveID = node.LeftChildIndex;
				const BVHPrimitive prim = primitiveBuffer.Load<BVHPrimitive>(primitiveID * sizeof(BVHPrimitive));
				//const BVHPrimitive prim = primitiveBuffer[primitiveID];
				//if (prim.flags & mask)
				{
					IntersectTriangle(ray, bestHit, prim);
				}
			}
			else
			{
				// Internal node
				if (stackpos < RAYTRACE_STACKSIZE - 1)
				{
					// push left child
					stack[stackpos++][groupIndex] = node.LeftChildIndex;
					// push right child
					stack[stackpos++][groupIndex] = node.RightChildIndex;
				}
				else
				{
					// stack overflow, terminate
					break;
				}
			}

		}

	} //while (stackpos > 0);


	return bestHit;
}

float3 LoadCurvePoint(int bufferAt)
{
	ByteAddressBuffer curve_points_buffer = bindless_buffers[descriptor_index(push.curvePointsBufferIndex)];
	uint3 data = curve_points_buffer.Load3(bufferAt * 12);
	return float3(asfloat(data.x), asfloat(data.y), asfloat(data.z));
}

RayDesc CreateCurvedSlicerRay(const uint2 pixel)
{
	// here, COS refers to Curved_Object_Space
	// Exploit ShaderCamera's parameters to store slicer's meta information
	ShaderCamera camera = GetCamera();
	const int2 rt_resolution = int2(camera.internal_resolution.x, camera.internal_resolution.y);
	const float plane_width_pixel = camera.frustum_corners.cornersFAR[0].w;
	const float pitch = camera.frustum_corners.cornersFAR[1].w;
	const float plane_height_pixel = camera.frustum_corners.cornersFAR[2].w;

	const float3 pos_cos_TL = camera.frustum_corners.cornersFAR[0].xyz;
	const float3 pos_cos_TR = camera.frustum_corners.cornersFAR[1].xyz;
	const float3 pos_cos_BL = camera.frustum_corners.cornersFAR[2].xyz;
	const float3 pos_cos_BR = camera.frustum_corners.cornersFAR[3].xyz;

	const float3 plane_up = camera.up;
	const bool is_right_side = push.sliceFlags & SLICER_FLAG_REVERSE_SIDE;
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
		|| push.curvePointsBufferIndex < 0)
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


	float3 pos_ws_c0 = LoadCurvePoint(lookup_index0);
	float3 pos_ws_c1 = LoadCurvePoint(lookup_index1);
	float3 pos_ws_c2 = LoadCurvePoint(lookup_index2);

	float3 pos_ws_c_ = pos_ws_c0 * (1.f - interpolate_ratio) + pos_ws_c1 * interpolate_ratio;

	float3 tan_ws_c0 = normalize(pos_ws_c1 - pos_ws_c0);
	float3 tan_ws_c1 = normalize(pos_ws_c2 - pos_ws_c1);
	float3 tan_ws_c_ = normalize(tan_ws_c0 * (1.f - interpolate_ratio) + tan_ws_c1 * interpolate_ratio);
	float3 ray_dir_ws = normalize(cross(plane_up, tan_ws_c_));

	if (is_right_side)
		ray_dir_ws *= -1.f;

	float3 ray_origin_ws = pos_ws_c_ + ray_dir_ws * (thickness_position - push.sliceThickness * 0.5f);

	// start position //
	ray.Origin = ray_origin_ws + plane_up * pitch * (pos_cos.y - plane_vertical_center_pixel);
	ray.Direction = ray_dir_ws;
	ray.TMin = 0.0f;
	return ray;
}