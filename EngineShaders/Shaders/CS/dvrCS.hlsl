#define K_NUM 2
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

	const float2 uv = ((float2)pixel + (float2)0.5) * camera.internal_resolution_rcp;
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
	int buffer_index_bitmask = vol_instance.bitmaskbuffer;

	if (texture_index_volume_main < 0)
		return;
	
	Texture3D volume_main = bindless_textures3D[descriptor_index(texture_index_volume_main)];
	Texture3D volume_mask = bindless_textures3D[descriptor_index(texture_index_volume_mask)];
	Texture3D volume_blocks = bindless_textures3D[descriptor_index(vol_instance.texture_volume_blocks)];
	Texture2D otf = bindless_textures[descriptor_index(texture_index_otf)];

	Buffer<uint> buffer_bitmask = bindless_buffers_uint[descriptor_index(buffer_index_bitmask)];
    
	int hit_step = -1;
	float3 pos_start_ws = ray.Origin + ray.Direction * hits_t.x;
    float3 dir_sample_ws = ray.Direction * vol_instance.sample_dist;

	const float3 singleblock_size_ts = vol_instance.ComputeSingleBlockTS();
	const uint2 blocks_wwh = uint2(vol_instance.num_blocks.x, vol_instance.num_blocks.x * vol_instance.num_blocks.y);

	SearchForemostSurface(hit_step, pos_start_ws, dir_sample_ws, num_ray_samples, vol_instance.mat_ws2ts, singleblock_size_ts, blocks_wwh, volume_main, buffer_bitmask
#if defined(OTF_MASK) || defined(SCULPT_MASK)
			, volume_mask
#endif
		);

	if (hit_step < 0)
		return;
	
	float3 pos_hit_ws = pos_start_ws + dir_sample_ws * (float)hit_step;
	if (hit_step > 0) {
		RefineSurface(pos_hit_ws, pos_hit_ws, dir_sample_ws, ITERATION_REFINESURFACE, vol_instance.mat_ws2ts, volume_main
#if defined(OTF_MASK) || defined(SCULPT_MASK)
				, volume_mask
#endif				
			);
		pos_hit_ws -= dir_sample_ws;
		if (dot(pos_hit_ws - pos_start_ws, dir_sample_ws) <= 0)
			pos_hit_ws = pos_start_ws;
	}

	float3 vec_o2start = pos_hit_ws - ray.Origin;
	float3 cam_forward = camera.forward; // supposed to be a unit vector
	float z_hit = dot(vec_o2start, cam_forward);
	float linear_depth = z_hit * camera.z_far_rcp;

	
	// TODO : PIXELMASK for drawing outline (if required!!)
	// 	store a value to the PIXEL MASK (bitpacking...)
	//	use RWBuffer<UINT> atomic operation...

	RWTexture2D<float4> inout_color = bindless_rwtextures[descriptor_index(push.inout_color_Index)];
	RWTexture2D<float> inout_linear_depth = bindless_rwtextures_float[descriptor_index(push.inout_linear_depth_Index)];

	float prev_z = inout_linear_depth[pixel];
	if (linear_depth >= prev_z)
		return;

	uint dvr_hit_enc = length(pos_hit_ws - pos_start_ws) < vol_instance.sample_dist ? DVR_SURFACE_ON_CLIPPLANE : DVR_SURFACE_OUTSIDE_CLIPPLANE;

	if (vol_instance.flags & INST_JITTERING)
	{
		RNG rng;
		rng.init(uint2(pixel), GetFrameCount());
		// additional feature : https://koreascience.kr/article/JAKO201324947256830.pdf
		float ray_dist_jitter = rng.next_float() * vol_instance.sample_dist;
		pos_hit_ws -= ray_dist_jitter * ray.Direction;
		linear_depth -= ray_dist_jitter;
		vec_o2start = pos_hit_ws - ray.Origin;
	}
	float ray_dist_o2start = length(vec_o2start);

	inout_linear_depth[pixel] = linear_depth;

	// TODO: PROGRESSIVE SCHEME
	// light map can be updated asynch as long as it can be used as UAV
	// 1. two types of UAVs : lightmap (using mesh's lightmap and ?!)

	// dvr depth??
	// inlinear depth , normal depth to ... tiled...
	// dominant light 1 and light field for multiple lights
	// tiled lighting?!
	// ... light map ...
	half opacity_correction = (half)push.opacity_correction;
	half sample_dist = (half)vol_instance.sample_dist;

	half4 color_out = (half4)0; // output
	float cos = dot(cam_forward, ray.Direction);

	float4 prev_color = inout_color[pixel].rgba;
	uint num_frags = 0;
	Fragment fs[K_NUM];
	if (prev_z < 1e20 && prev_z < 1.f)
	{
		Fragment f;
		//f.SetColor(half4(prev_color.rgb, (half)1.f));
		f.color_packed = pack_R11G11B10_rgba8(prev_color.rgb);
		float z = prev_z * camera.z_far;
		f.z = z / cos;
		f.zthick = sample_dist;
		f.opacity_sum = (half)1.f;
		fs[0] = f;
		num_frags++;
	}
	fs[num_frags].Init();

	//num_frags++;
	uint index_frag = 0;
	Fragment f_next_layer = fs[0];
	
	float3 dir_sample_ts = TransformVector(dir_sample_ws, vol_instance.mat_ws2ts); 
	float3 pos_ray_start_ts = TransformPoint(pos_hit_ws, vol_instance.mat_ws2ts);

	// --- gradient setting ---
	float3 v_v = dir_sample_ts;
	float3 uv_v = ray.Direction; // uv_v
	float3 uv_u = camera.up;
	float3 uv_r = cross(uv_v, uv_u); // uv_r
	uv_u = normalize(cross(uv_r, uv_v)); // uv_u , normalize?! for precision
	float3 v_u = TransformVector(uv_u * vol_instance.sample_dist, vol_instance.mat_ws2ts); // v_u
	float3 v_r = TransformVector(uv_r * vol_instance.sample_dist, vol_instance.mat_ws2ts); // v_r
	// -------------------------

	int start_step = 0;
	float sample_value = volume_main.SampleLevel(sampler_linear_clamp, pos_ray_start_ts, 0).r;

	float sample_value_prev = volume_main.SampleLevel(sampler_linear_clamp, pos_ray_start_ts - v_v, 0).r;


	if (dvr_hit_enc == DVR_SURFACE_ON_CLIPPLANE) // on the clip plane
	{
		half4 color = (half4)0;
		start_step++;

		// unlit here
#ifdef OTF_PREINTEGRATION
		if (Vis_Volume_And_Check_Slab(color, sample_value, sample_value_prev, pos_ray_start_ts, opacity_correction, volume_main, otf))
#else
		if (Vis_Volume_And_Check(color, sample_value, pos_ray_start_ts, opacity_correction, volume_main, otf))
#endif
		{
			IntermixSample(color_out, f_next_layer, index_frag, color, ray_dist_o2start, sample_dist, num_frags, fs);
		}
		sample_value_prev = sample_value;
	}
	
	int sample_count = 0;

	// load the base light 
	//uint bucket = 0;
	//uint bucket_bits = xForwardLightMask[bucket];
	//const uint bucket_bit_index = firstbitlow(bucket_bits);
	//const uint entity_index = bucket * 32 + bucket_bit_index;
	//ShaderEntity base_light = load_entity(lights().first_item() + entity_index);
	ShaderEntity base_light = load_entity(0);
	float3 L = (float3)base_light.GetDirection();
	float3 V = -ray.Direction;

	// in this version, simply consider the directional light
	[loop]
	for (int step = start_step; step < num_ray_samples; step++)
	{
		float3 pos_sample_ts = pos_ray_start_ts + dir_sample_ts * (float)step;

        LOAD_BLOCK_INFO(blkSkip, pos_sample_ts, dir_sample_ts, num_ray_samples, step, buffer_bitmask)

		[branch]
		if (blkSkip.visible)
		{
			[loop]
			for (int sub_step = 0; sub_step <= blkSkip.num_skip_steps; sub_step++)
			{
				//float3 pos_sample_blk_ws = pos_hit_ws + dir_sample_ws * (float) i;
				float3 pos_sample_blk_ts = pos_ray_start_ts + dir_sample_ts * (float)(step + sub_step);

				half4 color = (half4) 0;
				if (sample_value_prev < 0) {
					sample_value_prev = volume_main.SampleLevel(sampler_linear_clamp, pos_sample_blk_ts - v_v, 0).r;
				}
				sample_count++;
#if OTF_PREINTEGRATION
				if (Vis_Volume_And_Check_Slab(color, sample_value, sample_value_prev, pos_sample_blk_ts, opacity_correction, volume_main, otf))
#else
				if (Vis_Volume_And_Check(color, sample_value, pos_sample_blk_ts, opacity_correction, volume_main, otf))
#endif
				{
					float3 G = GradientVolume3(sample_value, sample_value_prev, pos_sample_blk_ts, 
												v_v, v_u, v_r, uv_v, uv_u, uv_r, volume_main);
					float length_G = length(G);

					half lighting = (half)1.f;
					if (length_G > 0) {
						// TODO using material attributes
						float3 N = G / length_G;
						lighting = (half)saturate(PhongBlinnVR(V, L, N, float4(0.3, 0.3, 1.0, 100)));

						// TODO
						// 
						// Surface surface;
						// surface.init();
						// TiledLighting or ForwardLighting
					}

					color = half4(lighting * color.rgb, color.a);
					float ray_dist = ray_dist_o2start + (float)(step + sub_step) * vol_instance.sample_dist;
					IntermixSample(color_out, f_next_layer, index_frag, color, ray_dist, sample_dist, num_frags, fs);

					if (color_out.a >= ERT_ALPHA_HALF)
					{
						sub_step = num_ray_samples;
						step = num_ray_samples;
						color_out.a = (half)1.f;
						break;
					}
				} // if (Vis_Volume_And_...
				sample_value_prev = sample_value;
			} // for (int sub_step = 0; sub_step <= blkSkip.num_skip_steps; sub_step++)
		} 
		else
		{
			sample_value_prev = -1;
		} // if (blkSkip.visible)
		step += blkSkip.num_skip_steps;
		//step -= 1;
	} // for (int step = start_step; step < num_ray_samples; step++)

	if (color_out.a < ERT_ALPHA_HALF)
	{
		for (; index_frag < num_frags; ++index_frag)
		{
			half4 color = fs[index_frag].GetColor();
			color_out += color * ((half)1.f - color_out.a);
		}
	}
	
    inout_color[pixel] = (float4)color_out;
}