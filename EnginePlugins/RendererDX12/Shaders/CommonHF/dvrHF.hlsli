#include "../Globals.hlsli"
#include "../ShaderInterop_DVR.h"
#include "surfaceHF.hlsli"
#include "raytracingHF.hlsli"

#define ITERATION_REFINESURFACE 5

PUSHCONSTANT(volume, VolumePushConstants);

const float SAMPLE_LEVEL_TEST = 0;

Texture3D input_volume : register(t0);
Texture3D input_volume_blocks : register(t1);
Texture3D input_volume_mask : register(t2);

// use 1024 or 2048 resolution
Texture2D<float4> input_otf : register(t3); // input_volume 
Texture2D<float4> input_mask_colorcode : register(t4);  // input_volume_mask

inline float3 TransformPoint(in float3 pos_src, in float4x4 matT)
{
	float4 pos_src_h = mul(matT, float4(pos_src, 1.f));
	return pos_src_h.xyz / pos_src_h.w;
}

inline float3 TransformVector(in float3 vec_src, in float4x4 matT)
{
	return mul((float3x3) matT, vec_src);
}

inline bool IsInsideClipBox(const in float3 pos_target, const in float4x4 mat_2_bs)
{
	float3 pos_target_bs = TransformPoint(pos_target, mat_2_bs);
	float3 _dot = (pos_target_bs + float3(0.5, 0.5, 0.5)) * (pos_target_bs - float3(0.5, 0.5, 0.5));
	return _dot.x <= 0 && _dot.y <= 0 && _dot.z <= 0;
}

inline float2 ComputeAaBbHits(const float3 pos_start, float3 pos_min, const float3 pos_max, const float3 vec_dir)
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

float2 ComputeClipBoxHits(const float3 pos_start, const float3 vec_dir, const float4x4 mat_vbox_2bs)
{
	float3 pos_src_bs = TransformPoint(pos_start, mat_vbox_2bs);
	float3 vec_dir_bs = TransformVector(vec_dir, mat_vbox_2bs);
	float2 hit_t = ComputeAaBbHits(pos_src_bs, float3(-0.5, -0.5, -0.5), float3(0.5, 0.5, 0.5), vec_dir_bs);
	return hit_t;
}

float2 ComputePlaneHits(const in float prev_t, const in float next_t, const in float3 pos_onplane, const in float3 vec_plane, const in float3 pos_start, const in float3 vec_dir)
{
	float2 hits_t = float2(prev_t, next_t);

	// H : vec_planeSVS, V : f3VecSampleSVS, A : f3PosIPSampleSVS, Q : f3PosPlaneSVS //
	// 0. Is ray direction parallel with plane's vector?
	float dot_HV = dot(vec_plane, vec_dir);
	if (dot_HV != 0)
	{
		// 1. Compute T for Position on Plane
		float fT = dot(vec_plane, pos_onplane - pos_start) / dot_HV;
		// 2. Check if on Cube
		if (fT > prev_t && fT < next_t)
		{
			// 3. Check if front or next position
			if (dot_HV < 0)
				hits_t.x = fT;
			else
				hits_t.y = fT;
		}
		else if (fT > prev_t && fT > next_t)
		{
			if (dot_HV < 0)
				hits_t.y = -FLT_MAX; // return;
			else
				; // conserve prev_t and next_t
		}
		else if (fT < prev_t && fT < next_t)
		{
			if (dot_HV < 0)
				;
			else
				hits_t.y = -FLT_MAX; // return;
		}
	}
	else
	{
		// Check Upperside of plane
		if (dot(vec_plane, pos_onplane - pos_start) <= 0)
			hits_t.y = -FLT_MAX; // return;
	}

	return hits_t;
}

bool IsInsideClipBound(const in float3 pos, const in uint flags, const in ClipBox clip_box)
{
	// Custom Clip Plane //
	if (flags & 0x1)
	{
        float3 pos_clipplane, vec_clipplane;
        clip_box.GetCliPlane(pos_clipplane, vec_clipplane);
		float3 ph = pos - pos_clipplane;
		if (dot(ph, vec_clipplane) > 0)
			return false;
	}
	
	if (flags & 0x2)
	{
		if (!IsInsideClipBox(pos, clip_box.mat_clipbox_ws2bs))
			return false;
	}
	return true;
}

float2 ComputeVBoxHits(const in float3 pos_start, const in float3 vec_dir, const in uint flags, 
    const in float4x4 mat_vbox_2bs, 
    const in ClipBox clip_box)
{
	// Compute VObject Box Enter and Exit //
	float2 hits_t = ComputeClipBoxHits(pos_start, vec_dir, mat_vbox_2bs);

	if (hits_t.y > hits_t.x)
	{
		// Custom Clip Plane //
		if (flags & 0x1)
		{
            float3 pos_clipplane, vec_clipplane;
            clip_box.GetCliPlane(pos_clipplane, vec_clipplane);

			float2 hits_clipplane_t = ComputePlaneHits(hits_t.x, hits_t.y, pos_clipplane, vec_clipplane, pos_start, vec_dir);

			hits_t.x = max(hits_t.x, hits_clipplane_t.x);
			hits_t.y = min(hits_t.y, hits_clipplane_t.y);
		}

		if (flags & 0x2)
		{
			float2 hits_clipbox_t = ComputeClipBoxHits(pos_start, vec_dir, clip_box.mat_clipbox_ws2bs);

			hits_t.x = max(hits_t.x, hits_clipbox_t.x);
			hits_t.y = min(hits_t.y, hits_clipbox_t.y);
		}
	}

	return hits_t;
}

struct BlockSkip
{
	float blk_value;
	int num_skip_steps;
};
BlockSkip ComputeBlockSkip(const float3 pos_start_ts, const float3 vec_sample_ts, const float3 volume_blocks_ts, const Texture3D vol_blocks)
{
	BlockSkip blk_v = (BlockSkip)0;
	int3 blk_id = pos_start_ts / volume_blocks_ts;
	blk_v.blk_value = vol_blocks.Load(int4(blk_id, 0)).r;

	float3 pos_min_ts = float3(blk_id.x * volume_blocks_ts.x, blk_id.y * volume_blocks_ts.y, blk_id.z * volume_blocks_ts.z);
	float3 pos_max_ts = pos_min_ts + volume_blocks_ts;
	float2 hits_t = ComputeAaBbHits(pos_start_ts, pos_min_ts, pos_max_ts, vec_sample_ts);
	float dist_skip_ts = hits_t.y - hits_t.x;
	
	// here, max is for avoiding machine computation error
	blk_v.num_skip_steps = max(int(dist_skip_ts), 0);
	return blk_v;
};

#define LOAD_BLOCK_INFO(BLK, P, V, N, I) \
    BlockSkip BLK = ComputeBlockSkip(P, V, volume.volume_blocks_ts, input_volume_blocks);\
    BLK.num_skip_steps = min(BLK.num_skip_steps, N - I - 1);

float4 ApplyOTF(const in float sample_v, const in Texture2D<float4> otf, const in float opacity_correction, const in int id)
{
	float4 color = otf.SampleLevel(sampler_point_wrap, float2(sample_v, id * volume.id2multiotf_convert), 0);
	color.rgb *= color.a * opacity_correction; // associated color
	return color;
}


float4 ApplyPreintOTF(const float sample_v, float sample_v_prev, const in Texture2D<float4> otf, const float opacity_correction, const in int id)
{
	float4 color = (float4)0;

	if (sample_v == sample_v_prev) sample_v_prev += 1 / volume.sample_range;

	int diff = sample_v - sample_v_prev;

	float diff_rcp = 1.f / (float)diff;

	float4 color0 = otf.SampleLevel(sampler_point_wrap, float2(sample_v_prev, id * volume.id2multiotf_convert), 0);
	float4 color1 = otf.SampleLevel(sampler_point_wrap, float2(sample_v, id * volume.id2multiotf_convert), 0);

	color.rgb = (color1.rgb - color0.rgb) * diff_rcp;
	color.a = (color1.a - color1.a) * diff_rcp * opacity_correction;
	color.rgb *= color.a; // associate color
	return color;
}

// Sample_Volume_And_Check for SearchForemostSurface
// Vis_Volume_And_Check for raycast-rendering
#ifdef POST_INTERPOLATION
bool Sample_Volume_And_Check(inout float sample_v, const float3 pos_sample_ts, const float visible_min_sample)
{
	sample_v = input_volume.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
	return sample_v > visible_min_value;
}
bool Vis_Volume_And_Check(inout float4 color, const float3 pos_sample_ts)
{
	float3 pos_vs = float3(pos_sample_ts.x * g_cbVobj.vol_size.x - 0.5f, 
		pos_sample_ts.y * g_cbVobj.vol_size.y - 0.5f, 
		pos_sample_ts.z * g_cbVobj.vol_size.z - 0.5f);
	int3 idx_vs = int3(pos_vs);

	float samples[8];
	samples[0] = input_volume.Load(int4(idx_vs, SAMPLE_LEVEL_TEST)).r;
	samples[1] = input_volume.Load(int4(idx_vs + int3(1, 0, 0), SAMPLE_LEVEL_TEST)).r;
	samples[2] = input_volume.Load(int4(idx_vs + int3(0, 1, 0), SAMPLE_LEVEL_TEST)).r;
	samples[3] = input_volume.Load(int4(idx_vs + int3(1, 1, 0), SAMPLE_LEVEL_TEST)).r;
	samples[4] = input_volume.Load(int4(idx_vs + int3(0, 0, 1), SAMPLE_LEVEL_TEST)).r;
	samples[5] = input_volume.Load(int4(idx_vs + int3(1, 0, 1), SAMPLE_LEVEL_TEST)).r;
	samples[6] = input_volume.Load(int4(idx_vs + int3(0, 1, 1), SAMPLE_LEVEL_TEST)).r;
	samples[7] = input_volume.Load(int4(idx_vs + int3(1, 1, 1), SAMPLE_LEVEL_TEST)).r;

	float3 f3InterpolateRatio = pos_vs - idx_vs;

	float fInterpolateWeights[8];
	fInterpolateWeights[0] = (1.f - f3InterpolateRatio.z) * (1.f - f3InterpolateRatio.y) * (1.f - f3InterpolateRatio.x);
	fInterpolateWeights[1] = (1.f - f3InterpolateRatio.z) * (1.f - f3InterpolateRatio.y) * f3InterpolateRatio.x;
	fInterpolateWeights[2] = (1.f - f3InterpolateRatio.z) * f3InterpolateRatio.y * (1.f - f3InterpolateRatio.x);
	fInterpolateWeights[3] = (1.f - f3InterpolateRatio.z) * f3InterpolateRatio.y * f3InterpolateRatio.x;
	fInterpolateWeights[4] = f3InterpolateRatio.z * (1.f - f3InterpolateRatio.y) * (1.f - f3InterpolateRatio.x);
	fInterpolateWeights[5] = f3InterpolateRatio.z * (1.f - f3InterpolateRatio.y) * f3InterpolateRatio.x;
	fInterpolateWeights[6] = f3InterpolateRatio.z * f3InterpolateRatio.y * (1.f - f3InterpolateRatio.x);
	fInterpolateWeights[7] = f3InterpolateRatio.z * f3InterpolateRatio.y * f3InterpolateRatio.x;

	color = (float4)0;
	[unroll]
	for (int m = 0; m < 8; m++) {
		float4 vis = ApplyOTF(samples[m], input_otf, volume.opacity_correction, 0);
		color += vis * fInterpolateWeights[m];
	}
	return color.a >= FLT_OPACITY_MIN;
}
#else
bool Sample_Volume_And_Check(inout float sample_v, const float3 pos_sample_ts, const float visible_min_sample)
{
	sample_v = input_volume.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;

#ifdef SCULPT_MASK
	int mask_vint = (int)(input_volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, 0).r * volume.mask_sample_range + 0.5f);
    return sample_v >= visible_min_sample && (mask_vint == 0 || mask_vint > volume.sculpt_value);
#else 
    return sample_v >= visible_min_sample;
#endif // SCULPT_MASK
}

bool Vis_Volume_And_Check(inout float4 color, inout float sample_v, const float3 pos_sample_ts)
{
	sample_v = input_volume.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
#ifdef OTF_MASK
	int mask_vint = (int)(input_volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r * volume.mask_sample_range + 0.5f);
	color = ApplyOTF(sample_v, input_otf, volume.opacity_correction, mask_vint);
	return color.a >= FLT_OPACITY_MIN;
#else
#ifdef SCULPT_MASK
	int mask_vint = (int)(input_volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r * volume.mask_sample_range + 0.5f);
    color = ApplyOTF(sample_v, input_otf, volume.opacity_correction, 0);
    return color.a >= FLT_OPACITY_MIN && (mask_vint == 0 || mask_vint > volume.sculpt_value);
#else 
    color = ApplyOTF(sample_v, input_otf, volume.opacity_correction, 0);
    return color.a >= FLT_OPACITY_MIN;
#endif // SCULPT_MASK
#endif // OTF_MASK
}

bool Vis_Volume_And_Check_Slab(inout float4 color, inout float sample_v, float sample_prev, const float3 pos_sample_ts)
{
	sample_v = input_volume.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;

#if OTF_MASK==1
	int mask_vint = (int)(input_volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r * volume.mask_sample_range + 0.5f);
	color = ApplyPreintOTF(sample_v, sample_prev, input_otf, volume.opacity_correction, mask_vint);
	return color.a >= FLT_OPACITY_MIN;
#elif SCULPT_MASK == 1
	int mask_vint = (int)(input_volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r * volume.mask_sample_range + 0.5f);
    color = ApplyPreintOTF(sample_v, sample_prev, input_otf, volume.opacity_correction, 0);
    return color.a >= FLT_OPACITY_MIN && (mask_vint == 0 || mask_vint > volume.sculpt_value);
#else 
    color = ApplyPreintOTF(sample_v, sample_prev, input_otf, volume.opacity_correction, 0);
    return color.a >= FLT_OPACITY_MIN;
#endif
}
#endif // POST_INTERPOLATION

void SearchForemostSurface(inout int step, const float3 pos_ray_start_ws, const float3 dir_sample_ws, const int num_ray_samples)
{
    step = -1;
    float sample_v = 0;

    float3 pos_ray_start_ts = TransformPoint(pos_ray_start_ws, volume.mat_ws2ts);
    float3 dir_sample_ts = TransformVector(dir_sample_ws, volume.mat_ws2ts);
    
	[branch]
    if (Sample_Volume_And_Check(sample_v, pos_ray_start_ts, volume.main_visible_min_sample))
    {
        step = 0;
        return;
    }

	[loop]
    for (int i = 1; i < num_ray_samples; i++)
    {
        float3 pos_sample_ts = pos_ray_start_ts + dir_sample_ts * (float) i;

        LOAD_BLOCK_INFO(blkSkip, pos_sample_ts, dir_sample_ts, num_ray_samples, i)
			
		[branch]
        if (blkSkip.blk_value > 0)
        {
	        [loop]
            for (int k = 0; k <= blkSkip.num_skip_steps; k++)
            {
                float3 pos_sample_blk_ts = pos_ray_start_ts + dir_sample_ts * (float) (i + k);
				[branch]
                if (Sample_Volume_And_Check(sample_v, pos_sample_blk_ts, min_valid_v))
                {
					step = i + k;
					i = num_ray_samples;
                    k = num_ray_samples;
                    break;
                } // if(sample valid check)
            } // for(int k = 0; k < blkSkip.iNumStepSkip; k++, i++)
        }
        //else
        //{
        //    i += blkSkip.num_skip_steps;
        //}
		i += blkSkip.num_skip_steps;
		// this is for outer loop's i++
        //i -= 1;
    }
}