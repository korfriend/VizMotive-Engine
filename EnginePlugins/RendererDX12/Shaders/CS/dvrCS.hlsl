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
	int buffer_index_bitmask = vol_instance.bitmaskbuffer;

	if (texture_index_volume_main < 0)
		return;
	
	Texture3D volume_main = bindless_textures3D[texture_index_volume_main];
	Texture3D volume_mask = bindless_textures3D[texture_index_volume_mask];
	Texture3D volume_blocks = bindless_textures3D[vol_instance.texture_volume_blocks];
	Texture2D otf = bindless_textures[texture_index_otf];

	Buffer<uint> buffer_bitmask = bindless_buffers_uint[buffer_index_bitmask];
    
	int hit_step = -1;
	float3 pos_start_ws = ray.Origin + ray.Direction * hits_t.x;
    float3 dir_sample_ws = ray.Direction * vol_instance.sample_dist;

	SearchForemostSurface(hit_step, pos_start_ws, dir_sample_ws, num_ray_samples, vol_instance.mat_ws2ts, volume_main, buffer_bitmask
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

	float3 ray_hit = pos_hit_ws - ray.Origin;
	float z_hit = dot(ray_hit, GetCamera().forward);
	float linear_depth = z_hit * GetCamera().z_far_rcp;
	if (linear_depth >= inout_linear_depth[DTid.xy])
		return;

	if (BitCheck(vol_instance.flags, INST_JITTERING))
	{
		RNG rng;
		rng.init(uint2(pixel), GetFrameCount());
		// additional feature : https://koreascience.kr/article/JAKO201324947256830.pdf
		pos_hit_ws -= rng.next_float() * vol_instance.sample_dist * ray.Direction;
	}

	uint dvr_hit_enc = length(pos_hit_ws - pos_start_ws) < vol_instance.sample_dist ? DVR_SURFACE_ON_CLIPPLANE : DVR_SURFACE_OUTSIDE_CLIPPLANE;

    if (dvr_hit_enc != DVR_SURFACE_OUTSIDE_VOLUME)
    {
        inout_color[DTid.xy] = float4(1, 1, 1, 1);
        inout_linear_depth[DTid.xy] = linear_depth;
    }

	// TODO: PROGRESSIVE SCHEME
	// light map can be updated asynch as long as it can be used as UAV
	// 1. two types of UAVs : lightmap (using mesh's lightmap and ?!)

	// dvr depth??
	// inlinear depth , normal depth to ... tiled...
	// dominant light 1 and light field for multiple lights
	// tiled lighting?!
	// ... light map ...

}