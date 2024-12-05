#include "../CommonHF/dvrHF.hlsli"

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<float> inout_linear_depth : register(u1);

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

	VolumeInstance vol_instance = GetVolumeInstance();
	ShaderClipper clipper; // TODO
	clipper.Init();

    // sample_dist (in world space scale)
    
	// Ray Intersection for Clipping Box //
	float2 hits_t = ComputeVBoxHits(ray.Origin, ray.Direction, vol_instance.flags, vol_instance.mat_alignedvbox_ws2bs, clipper);
	// 1st Exit in the case that there is no ray-intersecting boundary in the volume box
	hits_t.y = min(camera.z_far, hits_t.y); // only available in orthogonal view (thickness slicer)
	int num_ray_samples = (int)((hits_t.y - hits_t.x) / vol_instance.sample_dist + 0.5f);
	if (num_ray_samples <= 0)
		return;

	ShaderMaterial material = load_material(vol_instance.materialIndex);

	int texture_index_volume_main = material.volume_textures[VOLUME_MAIN_MAP].texture_descriptor;
	int texture_index_volume_mask = material.volume_textures[VOLUME_SEMANTIC_MAP].texture_descriptor;
	int texture_index_otf = material.lookup_textures[LOOKUP_OTF].texture_descriptor;

	if (texture_index_volume_main < 0)
		return;
	
	Texture3D volume_main = bindless_textures3D[texture_index_volume_main];
	Texture3D volume_mask = bindless_textures3D[texture_index_volume_mask];
	Texture3D volume_blocks = bindless_textures3D[vol_instance.texture_volume_blocks];
	Texture2D otf = bindless_textures[texture_index_otf];

    int hit_step = -1;
	float3 pos_start_ws = ray.Origin + ray.Direction * hits_t.x;
    float3 dir_sample_ws = ray.Direction * vol_instance.sample_dist;

	SearchForemostSurface(hit_step, pos_start_ws, dir_sample_ws, num_ray_samples, vol_instance.mat_ws2ts, volume_main, volume_blocks
#if defined(OTF_MASK) || defined(SCULPT_MASK)
			, volume_mask
#endif
		);

	//float sample_v = volume_blocks.SampleLevel(sampler_linear_clamp, float3(0.5, 0.5, 0.5), SAMPLE_LEVEL_TEST).r;
	//if (sample_v == 0)
	//	inout_color[DTid.xy] = float4(0, 0, 1, 1);
	//else if (sample_v > 0.99)
	//	inout_color[DTid.xy] = float4(0, 1, 0, 1);
	//else
	//	inout_color[DTid.xy] = float4(1, 0, 0, 1);

	if (hit_step < 0)
		return;
	
	float3 pos_hit_ws = pos_start_ws + dir_sample_ws * (float)hit_step;
	if (hit_step > 0) {
		RefineSurface(pos_hit_ws, pos_hit_ws, dir_sample_ws, ITERATION_REFINESURFACE, vol_instance.mat_ws2ts, volume_main, volume_blocks
#if defined(OTF_MASK) || defined(SCULPT_MASK)
				, volume_mask
#endif				
			);
		pos_hit_ws -= dir_sample_ws;
		if (dot(pos_hit_ws - pos_start_ws, dir_sample_ws) <= 0)
			pos_hit_ws = pos_start_ws;
	}

	float depth_hit = length(pos_hit_ws - ray.Origin);

	if (BitCheck(vol_instance.flags, INST_JITTERING))
	{
		RNG rng;
		rng.init(uint2(pixel), GetFrameCount());
		// additional feature : https://koreascience.kr/article/JAKO201324947256830.pdf
		depth_hit -= rng.next_float() * vol_instance.sample_dist;
	}

	uint dvr_hit_enc = length(pos_hit_ws - pos_start_ws) < vol_instance.sample_dist ? DVR_SURFACE_ON_CLIPPLANE : DVR_SURFACE_OUTSIDE_CLIPPLANE;

    if (dvr_hit_enc != DVR_SURFACE_OUTSIDE_VOLUME)
    {
        inout_color[DTid.xy] = float4(1, 0, 0, 1);
        inout_linear_depth[DTid.xy] = depth_hit * GetCamera().z_far_rcp;
    }
}