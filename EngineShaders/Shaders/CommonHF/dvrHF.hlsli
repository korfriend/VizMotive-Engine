#include "../Globals.hlsli"
#include "../ShaderInterop_DVR.h"
#include "surfaceHF.hlsli"
#include "raytracingHF.hlsli"
#include "zfragmentHF.hlsli"

#define ITERATION_REFINESURFACE 5

#define ERT_ALPHA 0.99
#define ERT_ALPHA_HALF (half)0.99

PUSHCONSTANT(push, VolumePushConstants);

static const float SAMPLE_LEVEL_TEST = 0;

//Texture3D input_volume : register(t0);
//Texture3D input_volume_blocks : register(t1);
//Texture3D input_volume_mask : register(t2);
//
//// use 1024 or 2048 resolution
//Texture2D<float4> input_otf : register(t3); // input_volume 
//Texture2D<float4> input_mask_colorcode : register(t4);  // input_volume_mask

// No need to be aligned by 16 bits (used only in shader)
struct VolumeInstance
{
	float4x4 mat_ws2ts;				// aliased from ShaderMeshInstance::transform
	float4x4 mat_alignedvbox_ws2bs; // aliased from ShaderMeshInstance::transformRaw
 
	uint materialIndex;				// aliased from ShaderMeshInstance::resLookupIndex
	float value_range;				// aliased from ShaderMeshInstance::geometryOffset
	float mask_value_range;		// aliased from ShaderMeshInstance::geometryCount , TODO: will be packed
    float mask_unormid_otf_map; // aliased from ShaderMeshInstance::baseGeometryOffset , this is for the difference between mask value's range and # of multi-OTF

	// we can texture_volume and texture_volume_mask from ShaderMaterial
	int texture_volume_blocks;		// aliased from ShaderMeshInstance::meshletOffset
	float sample_dist;				// WS unit, aliased from ShaderMeshInstance::alphaTest_size

	uint3 vol_size;		// aliased from ShaderMeshInstance::emissive, packing
	uint3 num_blocks;	// uint packing, aliased from ShaderMeshInstance::layerMask, packing
	uint3 block_pitches;	// uint packing, aliased from ShaderMeshInstance::baseGeometryCount, packing
	
	// the remaining stuffs can be used as the same purpose of ShaderMeshInstance's attributes
	uint uid; 
	uint flags;
	uint clipIndex; 
	float3 aabbCenter;
	float aabbRadius;
	float fadeDistance; 
	uint2 color;
	int lightmap;
	uint2 rimHighlight;

    void Init()
    {
        materialIndex = ~0u;
        flags = 0u;
        texture_volume_blocks = -1;
    }
	void Load(ShaderMeshInstance meshInst)
	{
		mat_ws2ts = meshInst.transform.GetMatrix();
		mat_alignedvbox_ws2bs = meshInst.transformRaw.GetMatrix();

		materialIndex = meshInst.resLookupIndex;
		value_range = asfloat(meshInst.geometryOffset);
		mask_value_range = asfloat(meshInst.geometryCount);
        mask_unormid_otf_map = asfloat(meshInst.baseGeometryOffset);

		texture_volume_blocks = asint(meshInst.meshletOffset);
		sample_dist = asfloat(meshInst.alphaTest_size);

		vol_size = uint3(meshInst.emissive.x, meshInst.emissive.y & 0xFFFF, meshInst.emissive.y >> 16);
		num_blocks = uint3((meshInst.layerMask >> 0) & 0x7FF, (meshInst.layerMask >> 11) & 0x7FF, (meshInst.layerMask >> 22) & 0x3FF);
		block_pitches = uint3((meshInst.baseGeometryCount >> 0) & 0x7FF, (meshInst.baseGeometryCount >> 11) & 0x7FF, (meshInst.baseGeometryCount >> 22) & 0x3FF);

		// the same attrobutes to ShaderMeshInstance
		uid = meshInst.uid;
		flags = meshInst.flags;
		clipIndex = meshInst.clipIndex;
		aabbCenter = meshInst.center;
		aabbRadius = meshInst.radius;
		fadeDistance = meshInst.fadeDistance;
		color = meshInst.color;
		lightmap = meshInst.lightmap;
		rimHighlight = meshInst.rimHighlight;
	}

	float3 ComputeSingleBlockTS() {
		return float3((float)block_pitches.x / vol_size.x,
			(float)block_pitches.y / vol_size.y, (float)block_pitches.z / vol_size.z);
	}
};

inline float3 TransformPoint(in float3 pos_src, in float4x4 matT)
{
	float4 pos_src_h = mul(matT, float4(pos_src, 1.f));
	return pos_src_h.xyz / pos_src_h.w;
}

inline float3 TransformVector(in float3 vec_src, in float4x4 matT)
{
	return mul((float3x3) matT, vec_src);
}

inline bool IsInsideClipBox(const float3 pos_target, const float4x4 mat_2_bs)
{
	float3 pos_target_bs = TransformPoint(pos_target, mat_2_bs);
	float3 _dot = (pos_target_bs + float3(0.5, 0.5, 0.5)) * (pos_target_bs - float3(0.5, 0.5, 0.5));
	return _dot.x <= 0 && _dot.y <= 0 && _dot.z <= 0;
}

inline float2 ComputeAaBbHits(const float3 pos_start, float3 pos_min, const float3 pos_max, const float3 vec_dir_rcp)
{
	// intersect ray with a box
	float3 invR = vec_dir_rcp;	// float3(1.0f, 1.0f, 1.0f) / vec_dir;
	float3 tbot = invR * (pos_min - pos_start);
	float3 ttop = invR * (pos_max - pos_start);

	// re-order intersections to find smallest and largest on each axis
	float3 tmin = min(ttop, tbot);
	float3 tmax = max(ttop, tbot);

	// find the largest tmin and the smallest tmax
	float largest_tmin = max(max(tmin.x, tmin.y), tmin.z);
	float smallest_tmax = min(min(tmax.x, tmax.y), tmax.z);

	float tnear = max(largest_tmin, 0.f);
	float tfar = smallest_tmax;
	return float2(tnear, tfar);
}

float2 ComputeClipBoxHits(const float3 pos_start, const float3 vec_dir, const float4x4 mat_vbox_2bs)
{
	float3 pos_src_bs = TransformPoint(pos_start, mat_vbox_2bs);
	float3 vec_dir_bs = TransformVector(vec_dir, mat_vbox_2bs);
	float3 vec_dir_bs_rcp = 1.f / vec_dir_bs;
	float2 hit_t = ComputeAaBbHits(pos_src_bs, float3(-0.5, -0.5, -0.5), float3(0.5, 0.5, 0.5), vec_dir_bs_rcp);
	return hit_t;
}

float2 ComputePlaneHits(const float prev_t, const float next_t, const float3 pos_onplane, const float3 vec_plane, const float3 pos_start, const float3 vec_dir)
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

bool IsInsideClipBound(const float3 pos, const uint flags, const ShaderClipper clipper)
{
	// Custom Clip Plane //
	if (flags & INST_CLIPPLANE)
	{
        float3 pos_clipplane, vec_clipplane;
        clipper.GetCliPlane(pos_clipplane, vec_clipplane);
		float3 ph = pos - pos_clipplane;
		if (dot(ph, vec_clipplane) > 0)
			return false;
	}
	
	if (flags & INST_CLIPBOX)
	{
		if (!IsInsideClipBox(pos, clipper.transformClibBox.GetMatrix()))
			return false;
	}
	return true;
}

float2 ComputeVBoxHits(const float3 pos_start, const float3 vec_dir, const uint flags, 
    const float4x4 mat_vbox_2bs, 
    const ShaderClipper clipper)
{
	// Compute VObject Box Enter and Exit //
	float2 hits_t = ComputeClipBoxHits(pos_start, vec_dir, mat_vbox_2bs);

	if (hits_t.y > hits_t.x)
	{
		// Custom Clip Plane //
		if (flags & INST_CLIPPLANE)
		{
            float3 pos_clipplane, vec_clipplane;
            clipper.GetCliPlane(pos_clipplane, vec_clipplane);

			float2 hits_clipplane_t = ComputePlaneHits(hits_t.x, hits_t.y, pos_clipplane, vec_clipplane, pos_start, vec_dir);

			hits_t.x = max(hits_t.x, hits_clipplane_t.x);
			hits_t.y = min(hits_t.y, hits_clipplane_t.y);
		}

		if (flags & INST_CLIPBOX)
		{
			float2 hits_clipbox_t = ComputeClipBoxHits(pos_start, vec_dir, clipper.transformClibBox.GetMatrix());

			hits_t.x = max(hits_t.x, hits_clipbox_t.x);
			hits_t.y = min(hits_t.y, hits_clipbox_t.y);
		}
	}

	return hits_t;
}

struct BlockSkip
{
	bool visible;
	int num_skip_steps;
};
BlockSkip ComputeBlockSkip(const float3 pos_start_ts, const float3 vec_sample_ts_rcp, const float3 singleblock_size_ts, const uint2 blocks_wwh, const Buffer<uint> buffer_bitmask)
{
	BlockSkip blk_v = (BlockSkip)0;
	int3 blk_id = pos_start_ts / singleblock_size_ts;
	int bitmask_id =  blk_id.x + blk_id.y * blocks_wwh.x + blk_id.z * blocks_wwh.y;
	int mod = bitmask_id % 32;
	blk_v.visible = (bool)(buffer_bitmask[bitmask_id / 32] & (0x1u << mod));

	float3 pos_min_ts = float3(blk_id.x * singleblock_size_ts.x, blk_id.y * singleblock_size_ts.y, blk_id.z * singleblock_size_ts.z);
	float3 pos_max_ts = pos_min_ts + singleblock_size_ts;
	float2 hits_t = ComputeAaBbHits(pos_start_ts, pos_min_ts, pos_max_ts, vec_sample_ts_rcp);
	float dist_skip_ts = hits_t.y - hits_t.x;
	if (dist_skip_ts < 0)
	{
		blk_v.visible = false;
		blk_v.num_skip_steps = 1000000;
	}
	else
	{
		// here, max is for avoiding machine computation error
		blk_v.num_skip_steps = max(int(dist_skip_ts), 1);	// sample step
	}
	
	return blk_v;
};

half4 ApplyOTF(const Texture2D otf, const float sample_v, const float idv, const half opacity_correction)
{
	half4 color = (half4)otf.SampleLevel(sampler_point_clamp, float2(sample_v, idv), 0);
	color.rgb *= color.a * opacity_correction; // associated color
	return color;
}

// in this case, Texture2D is stored as R32G32B32A32_FLOAT 
// 	the integration should be performed based on the float color of range [0,1]
half4 ApplyPreintOTF(const Texture2D otf, const float sample_v, float sample_v_prev, const float value_range, const float idv, const half opacity_correction)
{
	half4 color = (half4)0;

    if (sample_v == sample_v_prev)
    {
        sample_v_prev -= 1.f / value_range;
    }
	int diff = sample_v - sample_v_prev;

	half diff_rcp = (half)1.f / (half)diff;

	float4 color0 = otf.SampleLevel(sampler_point_clamp, float2(sample_v_prev, idv), 0);
	float4 color1 = otf.SampleLevel(sampler_point_clamp, float2(sample_v, idv), 0);

	color.rgb = (half3)(color1.rgb - color0.rgb) * diff_rcp;
	color.a = (half)(color1.a - color0.a) * diff_rcp * opacity_correction;
	color.rgb *= color.a; // associate color
	return color;
}

float3 GradientVolume3(const float sampleV, const float samplePreV, const float3 pos_sample_ts,
	const float3 vec_v, const float3 vec_u, const float3 vec_r, // TS from WS
	const float3 uvec_v, const float3 uvec_u, const float3 uvec_r,
	Texture3D tex3d_data)
{
	// note v, u, r are orthogonal for each other
	// vec_u and vec_r are defined in TS, and each length is sample distance
	// uvec_v, uvec_u, and uvec_r are unit vectors defined in WS
	//float dv = tex3d_data.SampleLevel(sampler_linear_clamp, pos_sample_ts - 1 * vec_v, 0).r - sampleV;
	float dv = samplePreV - sampleV;
	float du = tex3d_data.SampleLevel(sampler_linear_clamp, pos_sample_ts - 1 * vec_u, 0).r - sampleV;
	float dr = tex3d_data.SampleLevel(sampler_linear_clamp, pos_sample_ts - 1 * vec_r, 0).r - sampleV;

	float3 v_v = uvec_v * dv;
	float3 v_u = uvec_u * du;
	float3 v_r = uvec_r * dr;
	return v_v + v_u + v_r;
}

// Sample_Volume_And_Check for SearchForemostSurface
// Vis_Volume_And_Check for raycast-rendering
#ifdef POST_INTERPOLATION // do not support mask volume
bool Sample_Volume_And_Check(inout float sample_v, const float3 pos_sample_ts, const float visible_min_sample, const half opacity_correction,
	const Texture3D volume_main)
{
	sample_v = volume_main.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
	return sample_v > visible_min_value;
}
bool Vis_Volume_And_Check(inout half4 color, const float3 pos_sample_ts, const half opacity_correction,
	const Texture3D volume_main, const Texture2D otf, const half opacity_correction)
{
	float3 pos_vs = float3(pos_sample_ts.x * g_cbVobj.vol_size.x - 0.5f, 
		pos_sample_ts.y * g_cbVobj.vol_size.y - 0.5f, 
		pos_sample_ts.z * g_cbVobj.vol_size.z - 0.5f);
	int3 idx_vs = int3(pos_vs);

	float samples[8];
	samples[0] = volume_main.Load(int4(idx_vs, SAMPLE_LEVEL_TEST)).r;
	samples[1] = volume_main.Load(int4(idx_vs + int3(1, 0, 0), SAMPLE_LEVEL_TEST)).r;
	samples[2] = volume_main.Load(int4(idx_vs + int3(0, 1, 0), SAMPLE_LEVEL_TEST)).r;
	samples[3] = volume_main.Load(int4(idx_vs + int3(1, 1, 0), SAMPLE_LEVEL_TEST)).r;
	samples[4] = volume_main.Load(int4(idx_vs + int3(0, 0, 1), SAMPLE_LEVEL_TEST)).r;
	samples[5] = volume_main.Load(int4(idx_vs + int3(1, 0, 1), SAMPLE_LEVEL_TEST)).r;
	samples[6] = volume_main.Load(int4(idx_vs + int3(0, 1, 1), SAMPLE_LEVEL_TEST)).r;
	samples[7] = volume_main.Load(int4(idx_vs + int3(1, 1, 1), SAMPLE_LEVEL_TEST)).r;

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

	color = (half4)0;
	[unroll]
	for (int m = 0; m < 8; m++) {
		half4 vis = ApplyOTF(otf, samples[m], 0, opacity_correction);
		color += vis * (half)fInterpolateWeights[m];
	}
	return color.a >= (half)FLT_OPACITY_MIN;
}
#else
bool Sample_Volume_And_Check(inout float sample_v, const float3 pos_sample_ts, const float visible_min_sample, const half opacity_correction,
	const Texture3D volume_main
#ifdef SCULPT_MASK
	, const Texture3D volume_mask, const float mask_value_range,
#endif 
	)
{
	sample_v = volume_main.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;

#ifdef SCULPT_MASK
	int sculpt_step = (int)(volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, 0).r * mask_value_range + 0.5f);
    return sample_v >= visible_min_sample && (sculpt_step == 0 || sculpt_step > push.sculptStep);
#else 
    return sample_v >= visible_min_sample;
#endif // SCULPT_MASK
}

bool Vis_Volume_And_Check(inout half4 color, inout float sample_v, const float3 pos_sample_ts, const half opacity_correction,
	const Texture3D volume_main, const Texture2D otf
#if defined(OTF_MASK) || defined(SCULPT_MASK)
	, const Texture3D volume_mask, const float mask_value_range, const float mask_unormid_otf_map
#endif
	)
{
	sample_v = volume_main.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
#ifdef OTF_MASK
	float id = volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
	color = ApplyOTF(otf, sample_v, id * mask_unormid_otf_map, opacity_correction);
	return color.a >= (half)FLT_OPACITY_MIN;
#else
#ifdef SCULPT_MASK
	int sculpt_step = (int)(volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r * mask_value_range + 0.5f);
    color = ApplyOTF(otf, sample_v, 0, opacity_correction);
    return color.a >= (half)FLT_OPACITY_MIN && (mask_vint == 0 || sculpt_step > push.sculptStep);
#else 
    color = ApplyOTF(otf, sample_v, 0, opacity_correction);
    return color.a >= (half)FLT_OPACITY_MIN;
#endif // SCULPT_MASK
#endif // OTF_MASK
}

bool Vis_Volume_And_Check_Slab(inout half4 color, inout float sample_v, float sample_prev, const float value_range, const float3 pos_sample_ts,
	const half opacity_correction,
	const Texture3D volume_main, const Texture2D otf
#if defined(OTF_MASK) || defined(SCULPT_MASK)
	, const Texture3D volume_mask, const float mask_value_range, const float mask_unormid_otf_map
#endif
)
{
	sample_v = volume_main.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;

#if OTF_MASK==1
	float id = volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
	color = ApplyPreintOTF(otf, sample_v, sample_prev, id * mask_unormid_otf_map);
	return color.a >= (half)FLT_OPACITY_MIN;
#elif SCULPT_MASK == 1
	int sculpt_step = (int)(volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r * mask_value_range + 0.5f);
    color = ApplyPreintOTF(otf, sample_v, sample_prev, 0);
    return color.a >= (half)FLT_OPACITY_MIN && (sculpt_step == 0 || sculpt_step > push.sculptStep);
#else 
    color = ApplyPreintOTF(otf, sample_v, sample_prev, value_range, 0, opacity_correction);
    return color.a >= (half)FLT_OPACITY_MIN;
#endif
}
#endif // POST_INTERPOLATION


#define LOAD_BLOCK_INFO(BLK, P, V, N, I, BIT_MASK_BUFFER) \
    BlockSkip BLK = ComputeBlockSkip(P, V, singleblock_size_ts, blocks_wwh, BIT_MASK_BUFFER);\
    BLK.num_skip_steps = min(BLK.num_skip_steps, N - I - 1);

void SearchForemostSurface(inout int step, const float3 pos_ray_start_ws, const float3 dir_sample_ws, const int num_ray_samples, const float4x4 mat_ws2ts,
	const float3 singleblock_size_ts, const uint2 blocks_wwh,
	const Texture3D volume_main, const Buffer<uint> buffer_bitmask
#if defined(OTF_MASK) || defined(SCULPT_MASK)
	, const Texture3D volume_mask
#endif
	)
{
    step = -1;
    float sample_v = 0;

    float3 pos_ray_start_ts = TransformPoint(pos_ray_start_ws, mat_ws2ts);
    float3 dir_sample_ts = TransformVector(dir_sample_ws, mat_ws2ts);
	half opacity_correction = (half)push.opacity_correction;

	float3 dir_sample_ts_rcp = safe_rcp3(dir_sample_ts);
    
	[branch]
    if (Sample_Volume_And_Check(sample_v, pos_ray_start_ts, push.main_visible_min_sample, opacity_correction, volume_main
#ifdef SCULPT_MASK
		, volume_mask 
#endif
		))
    {
        step = 0;
        return;
    }

	[loop]
    for (int i = 1; i < num_ray_samples; i++)
    {
        float3 pos_sample_ts = pos_ray_start_ts + dir_sample_ts * (float) i;

        LOAD_BLOCK_INFO(blkSkip, pos_sample_ts, dir_sample_ts_rcp, num_ray_samples, i, buffer_bitmask)
			
		[branch]
        if (blkSkip.visible)
        {
			//step = 1;
			//break;
	        [loop]
            for (int k = 0; k < blkSkip.num_skip_steps; k++)
            {
                float3 pos_sample_blk_ts = pos_ray_start_ts + dir_sample_ts * (float) (i + k);
				[branch]
                if (Sample_Volume_And_Check(sample_v, pos_sample_blk_ts, push.main_visible_min_sample, opacity_correction, volume_main
#if defined(OTF_MASK) || defined(SCULPT_MASK)
						, volume_mask
#endif
					))
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

void RefineSurface(inout float3 pos_refined_ws, const float3 pos_sample_ws, const float3 dir_sample_ws, const int num_refinement, const float4x4 mat_ws2ts, 
	const Texture3D volume_main
#if defined(OTF_MASK) || defined(SCULPT_MASK)
	, const Texture3D volume_mask 
#endif
	)
{
    float3 pos_sample_ts = TransformPoint(pos_sample_ws, mat_ws2ts);
    float3 dir_sample_ts = TransformVector(dir_sample_ws, mat_ws2ts);
    float t0 = 0, t1 = 1;
    
	// Interval bisection
    float3 pos_bis_s_ts = pos_sample_ts - dir_sample_ts;
    float3 pos_bis_e_ts = pos_sample_ts;
	half opacity_correction = (half)push.opacity_correction;
	
	[loop]
    for (int j = 0; j < num_refinement; j++)
    {
        float3 pos_bis_ts = (pos_bis_s_ts + pos_bis_e_ts) * 0.5f;
        float t = (t0 + t1) * 0.5f;
        float _sample_v = 0;
        if (Sample_Volume_And_Check(_sample_v, pos_bis_ts, push.main_visible_min_sample, opacity_correction, volume_main
#if defined(OTF_MASK) || defined(SCULPT_MASK)
				, volume_mask 
#endif
			))
        {
            pos_bis_e_ts = pos_bis_ts;
            t1 = t;
        }
        else
        {
            pos_bis_s_ts = pos_bis_ts;
            t0 = t;
        }
    }

    pos_refined_ws = pos_sample_ws + dir_sample_ws * (t1 - 1.f);
}

static const uint DVR_SURFACE_OUTSIDE_VOLUME = 0u;
static const uint DVR_SURFACE_OUTSIDE_CLIPPLANE = 1u;
static const uint DVR_SURFACE_ON_CLIPPLANE = 2u;

VolumeInstance GetVolumeInstance()
{
    if (push.instanceIndex >= 0)
    {
        ShaderMeshInstance mesh_instance = load_instance(push.instanceIndex);
        VolumeInstance vol_instance;
        vol_instance.Load(mesh_instance);
        return vol_instance;
    }
    VolumeInstance inst;
    inst.Init(); // texture_volume_blocks = -1;
    return inst;
}

inline void IntermixSample(inout half4 colorIntegrated, inout Fragment f_next_layer, inout uint indexFrag, 
	const half4 color, const float rayDist, const half sampleThick, 
	const uint numFrags, const Fragment fs[K_NUM])
{
	// rayDist refers to the distance from the ray origin to the backside of the sample slab 
    if (indexFrag >= numFrags)
    {
        colorIntegrated += color * ((half)1.f - colorIntegrated.a);
    }
    else
    {
        Fragment f_cur;
		/*conversion to integer causes some color difference.. but negligible*/
        f_cur.color = color;

        f_cur.z = rayDist;
        f_cur.zthick = sampleThick;
		f_cur.opacity_sum = color.a;

		[loop]
        while (indexFrag < numFrags && f_cur.color.a >= SAFE_MIN_HALF)
        {
			if (f_cur.z < f_next_layer.z - (float)f_next_layer.zthick)
            {
				colorIntegrated += color * ((half)1.f - colorIntegrated.a);
				f_cur.color = (half4)0;
                break;
            }
			else if (f_next_layer.z < f_cur.z - (float)f_cur.zthick)
            {
				colorIntegrated += f_next_layer.color * ((half)1.f - colorIntegrated.a);
				f_next_layer = fs[++indexFrag];
            }
			else
			{
				Fragment f_prior, f_posterior; 
				if (f_cur.z > f_next_layer.z)
				{
					f_prior = f_next_layer; 
					f_posterior = f_cur; 
				}
				else
				{
					f_prior = f_cur; 
					f_posterior = f_next_layer; 
				}
				Fragment f_0_out, f_1_out;
				OverlapFragments(f_prior, f_posterior, f_0_out, f_1_out);
				colorIntegrated += f_0_out.color * ((half)1.f - colorIntegrated.a);
				if (f_cur.z > f_next_layer.z)
				{
					f_cur = f_1_out;
					f_next_layer = fs[++indexFrag];
				}
				else
				{
					f_cur.color = (half4)0;
					if (f_1_out.color.a < SAFE_MIN_HALF)
					{
						f_next_layer = fs[++indexFrag];
					}
					else
					{
						f_next_layer = f_1_out;
					}
					break;
				}
			}
			if (colorIntegrated.a > ERT_ALPHA_HALF)
			{
				//indexFrag = numFrags;
				break;
			}

        } // while (indexFrag < numFrags && f_cur.color.a > 0)
		if (f_cur.color.a >= SAFE_MIN_HALF)
		{
			colorIntegrated += f_cur.color * ((half)1.f - colorIntegrated.a);
		}
    } // if (indexFrag >= numFrags)
}

// toward directions from the fragment
float PhongBlinnVR(const float3 V, const float3 L, float3 N, const float4 phongFactor)
{
    float diff = max(dot(L, N), 0.f);
	float3 H = normalize(V + L);
	float specular = pow(max(dot(N, H), 0.0), phongFactor.w);
	return phongFactor.x + phongFactor.y * diff + phongFactor.z * specular;
}