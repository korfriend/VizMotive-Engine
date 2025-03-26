#include "../Globals.hlsli"
#include "../ShaderInterop_BVH.h"
#include "../ShaderInterop_DVR.h"

PUSHCONSTANT(push, SlicerMeshPushConstants);

#define primitiveCounterBuffer bindless_buffers[descriptor_index(push.BVH_counter)]
#define bvhNodeBuffer bindless_buffers[descriptor_index(push.BVH_nodes)]
#define primitiveBuffer bindless_buffers[descriptor_index(push.BVH_primitives)]

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

static const float eps = 1e-6;

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
	if (abs(det) < eps)
		return;
	float invDet = rcp(det);

	float3 tvec = ray.Origin - prim.v0();
	float u = dot(tvec, pvec) * invDet;
	if (u < -eps || u > 1 + eps)
		return;

	float3 qvec = cross(tvec, v0v1);
	float v = dot(ray.Direction, qvec) * invDet;
	if (v < -eps || u + v > 1 + eps)
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

inline bool IntersectNode(
	in RayDesc ray,
	in BVHNode box,
	in float3 rcpDirection,
	in float primitive_best_distance
)
{
	// check if the ray Origin is inside the node
	//bool originInside = (ray.Origin.x >= box.min.x && ray.Origin.x <= box.max.x &&
	//	ray.Origin.y >= box.min.y && ray.Origin.y <= box.max.y &&
	//	ray.Origin.z >= box.min.z && ray.Origin.z <= box.max.z);
	//float3 check = (ray.Origin - box.max) * (ray.Origin - box.min);
	//bool originInside = check.x <= 0 && check.y <= 0 && check.z <= 0;
	//if (originInside) {
	//	return true; // always TRUE intersection when the ray Origin is inside the node
	//}

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

// Returns the closest hit primitive if any (useful for generic trace). If nothing was hit, then rayHit.distance will be equal to FLT_MAX
inline RayHit TraceRay_Closest(RayDesc ray, uint groupIndex = 0)
{
	if (abs(ray.Direction.x) < eps) ray.Direction.x = 0.0001234f; // trick... for avoiding zero block skipping error
	if (abs(ray.Direction.y) < eps) ray.Direction.y = 0.0001234f; // trick... for avoiding zero block skipping error
	if (abs(ray.Direction.z) < eps) ray.Direction.z = 0.0001234f; // trick... for avoiding zero block skipping error

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

	uint count = 0;

	[loop]
	while (stackpos > 0 && count < 5000u) {
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
		count++;

	} //while (stackpos > 0);


	return bestHit;
}
