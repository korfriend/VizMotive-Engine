struct PostRenderer {
    float3 posCam; // WS
    int lightColor;

    float3 posLight; // WS
    float lightIntensity;

    float4x4 matPS2WS;

    float2 rtSize;
    float2 dummy1;

    float3 disBoxCenter; // WS
    float distBoxSize; // WS
};

float2 ComputeAaBbHits(const float3 pos_start, const float3 pos_min, const float3 pos_max, const float3 vec_dir)
{
	// intersect ray with a box
	// http://www.siggraph.org/education/materials/HyperGraph/raytrace/rtinter3.htm
	float3 invR = float3(1.0f, 1.0f, 1.0f) / vec_dir;
	float3 tbot = invR * (pos_min - pos_start);
	float3 ttop = invR * (pos_max - pos_start);

	// re-order intersections to find smallest and largest on each axis
	float3 tmin = min(ttop, tbot);
	float3 tmax = max(ttop, tbot);

	// find the largest tmin and the smallest tmax
	float largest_tmin = max(max(tmin.x, tmin.y), max(tmin.x, tmin.z));
	float smallest_tmax = min(min(tmax.x, tmax.y), min(tmax.x, tmax.z));

	float tnear = max(largest_tmin, 0.f);
	float tfar = smallest_tmax;
	return float2(tnear, tfar);
}