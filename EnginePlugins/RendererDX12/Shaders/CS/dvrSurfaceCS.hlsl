#include "../CommonHF/dvrHF.hlsli"

[numthreads(DVR_BLOCKSIZE, DVR_BLOCKSIZE, 1)]
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

    // sample_dist (in world space scale)
    
	// Ray Intersection for Clipping Box //
	float2 hits_t = ComputeVBoxHits(ray.Origin, ray.Direction, volume.mat_alignedvbox_ws2bs, volume.clip_box);
	// 1st Exit in the case that there is no ray-intersecting boundary in the volume box
	hits_t.y = min(camera.z_far, hits_t.y); // only available in orthogonal view (thickness slicer)
	int num_ray_samples = (int)((hits_t.y - hits_t.x) / volume.sample_dist + 0.5f);
	if (num_ray_samples <= 0)
		return;

    int hit_step = -1;
	float3 pos_start_ws = ray.Origin + ray.Direction * hits_t.x;
    float3 dir_sample_ws = ray.Direction * volume.sample_dist;
	Find1stSampleHit(hit_step, pos_start_ws, dir_sample_ws, num_ray_samples);
	if (hit_step < 0)
		return;
	
	float3 pos_hit_ws = pos_start_ws + dir_sample_ws * (float)hit_step;
	if (hit_step > 0) {
		FindNearestInsideSurface(pos_hit_ws, pos_hit_ws, dir_sample_ws, ITERATION_REFINESURFACE);
		pos_hit_ws -= dir_sample_ws;
		if (dot(pos_hit_ws - pos_start_ws, dir_sample_ws) <= 0)
			pos_hit_ws = pos_start_ws;
	}

	float depth_hit = length(pos_hit_ws - pos_ip_ws);

	if (BitCheck(g_cbVolMaterial.flag, 0))
	{
		// additional feature : https://koreascience.kr/article/JAKO201324947256830.pdf
		float rand = _random(float2(tex2d_xy.x + g_cbCamState.rt_width * tex2d_xy.y, depth_hit));
		depth_hit -= rand * g_cbVobj.sample_dist;
		//float3 random_samples = float3(_random(DTid.x + g_cbCamState.rt_width * DTid.y), _random(DTid.x + g_cbCamState.rt_width * DTid.y + g_cbCamState.rt_width * g_cbCamState.rt_height), _random(DTid.xy));
	}

	uint dvr_hit_enc = length(pos_hit_ws - pos_start_ws) < g_cbVobj.sample_dist ? 2 : 1;


	vr_fragment_1sthit_write[DTid.xy] = depth_hit;
	uint fcnt = fragment_counter[DTid.xy];
	// 2 : on the clip plane
	// 1 : outside the clip plane
	// 0 : outside the volume
	fragment_counter[DTid.xy] = fcnt | (dvr_hit_enc << 24);
}