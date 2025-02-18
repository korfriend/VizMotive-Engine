#include "../Globals.hlsli"
#include "../ShaderInterop_DVR.h"
#include "surfaceHF.hlsli"
#include "raytracingHF.hlsli"

#define ITERATION_REFINESURFACE 5

#define FRAG_MERGING
#define ERT_ALPHA 0.98
#define MAX_FRAGMENTS 2
#define SAFE_OPAQUEALPHA 0.99 // to avoid zero-divide

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
	float mask_value_range;		// aliased from ShaderMeshInstance::geometryCount
    float mask_unormid_otf_map; // aliased from ShaderMeshInstance::baseGeometryOffset

	int bitmaskbuffer;				// aliased from ShaderMeshInstance::baseGeometryCount
	// we can texture_volume and texture_volume_mask from ShaderMaterial
	int texture_volume_blocks;		// aliased from ShaderMeshInstance::meshletOffset
	float sample_dist;				// WS unit, aliased from ShaderMeshInstance::alphaTest_size

	uint3 vol_size;		// aliased from ShaderMeshInstance::emissive, packing
	uint3 num_blocks;	// uint packing, aliased from ShaderMeshInstance::layerMask, packing
	uint3 block_pitches;	// uint packing, aliased from ShaderMeshInstance::padding0, packing
	
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

#ifndef __cplusplus
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

		bitmaskbuffer = asint(meshInst.baseGeometryCount);
		vol_size = uint3(meshInst.emissive.x, meshInst.emissive.y & 0xFFFF, meshInst.emissive.y >> 16);
		num_blocks = uint3((meshInst.layerMask >> 0) & 0x7FF, (meshInst.layerMask >> 11) & 0x7FF, (meshInst.layerMask >> 22) & 0x3FF);
		block_pitches = uint3((meshInst.padding0 >> 0) & 0x7FF, (meshInst.padding0 >> 11) & 0x7FF, (meshInst.padding0 >> 22) & 0x3FF);

		// the same attrobutes to ShaderMeshInstance
		uid = meshInst.uid;
		flags = meshInst.flags;
		clipIndex = meshInst.clipIndex;
		aabbCenter = meshInst.aabbCenter;
		aabbRadius = meshInst.aabbRadius;
		fadeDistance = meshInst.fadeDistance;
		color = meshInst.color;
		lightmap = meshInst.lightmap;
		rimHighlight = meshInst.rimHighlight;
	}
#endif

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
BlockSkip ComputeBlockSkip(const float3 pos_start_ts, const float3 vec_sample_ts, const float3 singleblock_size_ts, const uint2 blocks_wwh, const Buffer<uint> buffer_bitmask)
{
	BlockSkip blk_v = (BlockSkip)0;
	int3 blk_id = pos_start_ts / singleblock_size_ts;
	int bitmask_id =  blk_id.x + blk_id.y * blocks_wwh.x + blk_id.z * blocks_wwh.y;
	int mod = bitmask_id % 32;
	blk_v.visible = (bool)(buffer_bitmask[bitmask_id / 32] & (0x1u << mod));

	float3 pos_min_ts = float3(blk_id.x * singleblock_size_ts.x, blk_id.y * singleblock_size_ts.y, blk_id.z * singleblock_size_ts.z);
	float3 pos_max_ts = pos_min_ts + singleblock_size_ts;
	float2 hits_t = ComputeAaBbHits(pos_start_ts, pos_min_ts, pos_max_ts, vec_sample_ts);
	float dist_skip_ts = hits_t.y - hits_t.x;
	
	// here, max is for avoiding machine computation error
	blk_v.num_skip_steps = max(int(dist_skip_ts), 0);
	return blk_v;
};

float4 ApplyOTF(const Texture2D otf, const float sample_v, const float idv)
{
	float4 color = otf.SampleLevel(sampler_point_wrap, float2(sample_v, idv), 0);
	color.rgb *= color.a * push.opacity_correction; // associated color
	return color;
}

// in this case, Texture2D is stored as R32G32B32A32_FLOAT 
// 	the integration should be performed based on the float color of range [0,1]
float4 ApplyPreintOTF(const Texture2D otf, const float sample_v, float sample_v_prev, const float value_range, const float idv)
{
	float4 color = (float4)0;

    if (sample_v == sample_v_prev)
    {
        sample_v_prev -= 1.f / value_range;
    }
	int diff = sample_v - sample_v_prev;

	float diff_rcp = 1.f / (float)diff;

	float4 color0 = otf.SampleLevel(sampler_point_wrap, float2(sample_v_prev, idv), 0);
	float4 color1 = otf.SampleLevel(sampler_point_wrap, float2(sample_v, idv), 0);

	color.rgb = (color1.rgb - color0.rgb) * diff_rcp;
	color.a = (color1.a - color1.a) * diff_rcp * push.opacity_correction;
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
bool Sample_Volume_And_Check(inout float sample_v, const float3 pos_sample_ts, const float visible_min_sample,
	const Texture3D volume_main)
{
	sample_v = volume_main.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
	return sample_v > visible_min_value;
}
bool Vis_Volume_And_Check(inout float4 color, const float3 pos_sample_ts,
	const Texture3D volume_main, const Texture2D otf)
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

	color = (float4)0;
	[unroll]
	for (int m = 0; m < 8; m++) {
		float4 vis = ApplyOTF(otf, samples[m], push.opacity_correction, 0);
		color += vis * fInterpolateWeights[m];
	}
	return color.a >= FLT_OPACITY_MIN;
}
#else
bool Sample_Volume_And_Check(inout float sample_v, const float3 pos_sample_ts, const float visible_min_sample,
	const Texture3D volume_main
#ifdef SCULPT_MASK
	, const Texture3D volume_mask
#endif 
	)
{
	sample_v = volume_main.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;

#ifdef SCULPT_MASK
	int sculpt_step = (int)(volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, 0).r * push.mask_value_range + 0.5f);
    return sample_v >= visible_min_sample && (sculpt_step == 0 || sculpt_step > push.sculptStep);
#else 
    return sample_v >= visible_min_sample;
#endif // SCULPT_MASK
}

bool Vis_Volume_And_Check(inout float4 color, inout float sample_v, const float3 pos_sample_ts,
	const Texture3D volume_main, const Texture2D otf
#if defined(OTF_MASK) || defined(SCULPT_MASK)
	, const Texture3D volume_mask
#endif
	)
{
	sample_v = volume_main.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
#ifdef OTF_MASK
	float id = volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
	color = ApplyOTF(otf, sample_v, id * push.mask_unormid_otf_map);
	return color.a >= FLT_OPACITY_MIN;
#else
#ifdef SCULPT_MASK
	int sculpt_step = (int)(volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r * push.mask_value_range + 0.5f);
    color = ApplyOTF(otf, sample_v, 0);
    return color.a >= FLT_OPACITY_MIN && (mask_vint == 0 || sculpt_step > push.sculptStep);
#else 
    color = ApplyOTF(otf, sample_v, 0);
    return color.a >= FLT_OPACITY_MIN;
#endif // SCULPT_MASK
#endif // OTF_MASK
}

bool Vis_Volume_And_Check_Slab(inout float4 color, inout float sample_v, float sample_prev, const float value_range, const float3 pos_sample_ts,
	const Texture3D volume_main, const Texture2D otf
#if defined(OTF_MASK) || defined(SCULPT_MASK)
	, const Texture3D volume_mask
#endif
)
{
	sample_v = volume_main.SampleLevel(sampler_linear_clamp, pos_sample_ts, SAMPLE_LEVEL_TEST).r;

#if OTF_MASK==1
	float id = volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r;
	color = ApplyPreintOTF(otf, sample_v, sample_prev, id * push.mask_unormid_otf_map);
	return color.a >= FLT_OPACITY_MIN;
#elif SCULPT_MASK == 1
	int sculpt_step = (int)(volume_mask.SampleLevel(sampler_point_wrap, pos_sample_ts, SAMPLE_LEVEL_TEST).r * push.mask_value_range + 0.5f);
    color = ApplyPreintOTF(otf, sample_v, sample_prev, 0);
    return color.a >= FLT_OPACITY_MIN && (sculpt_step == 0 || sculpt_step > push.sculptStep);
#else 
    color = ApplyPreintOTF(otf, sample_v, sample_prev, value_range, 0);
    return color.a >= FLT_OPACITY_MIN;
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
    
	[branch]
    if (Sample_Volume_And_Check(sample_v, pos_ray_start_ts, push.main_visible_min_sample, volume_main
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

        LOAD_BLOCK_INFO(blkSkip, pos_sample_ts, dir_sample_ts, num_ray_samples, i, buffer_bitmask)
			
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
                if (Sample_Volume_And_Check(sample_v, pos_sample_blk_ts, push.main_visible_min_sample, volume_main
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
	
	[loop]
    for (int j = 0; j < num_refinement; j++)
    {
        float3 pos_bis_ts = (pos_bis_s_ts + pos_bis_e_ts) * 0.5f;
        float t = (t0 + t1) * 0.5f;
        float _sample_v = 0;
        if (Sample_Volume_And_Check(_sample_v, pos_bis_ts, push.main_visible_min_sample, volume_main
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

struct Fragment
{
	float4 color; 
	float rayDist; // note that the distance along the view-ray, not z value in CS
#ifdef FRAG_MERGING
	float thick;
	float opacitySum;
#endif
};

struct Fragment_OUT
{
	Fragment f_prior; // Includes current intermixed depth
	Fragment f_posterior;
};


float4 MixOpt(const float4 vis1, const float alphaw1, const float4 vis2, const float alphaw2)
{
	float4 vout = (float4)0;
	if (alphaw1 + alphaw2 > 0)
	{
		float3 C_mix1 = vis1.rgb / vis1.a * alphaw1;
		float3 C_mix2 = vis2.rgb / vis2.a * alphaw2;
		float3 I_mix = (C_mix1 + C_mix2) / (alphaw1 + alphaw2);
		float T_mix1 = 1 - vis1.a;
		float T_mix2 = 1 - vis2.a;
		float A_mix = 1 - T_mix1 * T_mix2;
		vout = float4(I_mix * A_mix, A_mix);
	}
	return vout;
}

#ifdef FRAG_MERGING
#define LINEAR_MODE 1
Fragment_OUT MergeFrags(Fragment f_prior, Fragment f_posterior, const float beta)
{
	// Overall algorithm computation cost 
	// : 3 branches, 2 visibility interpolations, 2 visibility integrations, and 1 fusion of overlapping ray-segments

	// f_prior and f_posterior mean f_prior.rayDist >= f_prior.z
	Fragment_OUT fs_out;

	float zfront_posterior_f = f_posterior.rayDist - f_posterior.thick;
	if (f_prior.rayDist <= zfront_posterior_f)
	{
		// Case 1 : No Overlapping
		fs_out.f_prior = f_prior;
	}
	else
	{
		float4 f_m_prior_vis;
		float4 f_prior_vis = f_prior.color;
		float4 f_posterior_vis = f_posterior.color;
		f_prior_vis.a = min(f_prior_vis.a, SAFE_OPAQUEALPHA);
		f_posterior_vis.a = min(f_posterior_vis.a, SAFE_OPAQUEALPHA);

		float zfront_prior_f = f_prior.rayDist - f_prior.thick;
		if (zfront_prior_f <= zfront_posterior_f)
		{
			// Case 2 : Intersecting each other
			//fs_out.f_prior.thick = max(zfront_posterior_f - zfront_prior_f, 0); // to avoid computational precision error (0 or small minus)
			fs_out.f_prior.thick = zfront_posterior_f - zfront_prior_f;
			fs_out.f_prior.rayDist = zfront_posterior_f;
#if LINEAR_MODE == 1
			{
				f_m_prior_vis = f_prior_vis * (fs_out.f_prior.thick / f_prior.thick);
			}
#else
			{
				float zd_ratio = fs_out.f_prior.thick / f_prior.thick;
				float Ad = f_prior_vis.a;
				float3 Id = f_prior_vis.rgb / Ad;

				// strict mode
				float Az = Ad * zd_ratio * beta + (1 - beta) * (1 - pow(abs(1 - Ad), zd_ratio));
				// approx. mode
				//float term1 = zd_ratio * (1 - zd_ratio) / 2.f * Ad * Ad;
				//float term2 = term1 * (2 - zd_ratio) / 3.f * Ad;
				//float term3 = term2 * (3 - zd_ratio) / 4.f * Ad;
				//float term4 = term3 * (4 - zd_ratio) / 5.f * Ad;
				//float Az = Ad * zd_ratio + (1 - beta) * (term1 + term2 + term3 + term4 + term4 * (5 - zd_ratio) / 6.f * Ad);

				float3 Cz = Id * Az;
				f_m_prior_vis = float4(Cz, Az);
			}
#endif

			float old_alpha = f_prior_vis.a;
			f_prior.thick -= fs_out.f_prior.thick;
			f_prior_vis = (f_prior_vis - f_m_prior_vis) / (1.f - f_m_prior_vis.a);

			fs_out.f_prior.opacitySum = f_prior.opacitySum * f_m_prior_vis.a / old_alpha;
			f_prior.opacitySum = f_prior.opacitySum - fs_out.f_prior.opacitySum;
		}
		else
		{
			// Case 3 : f_prior belongs to f_posterior
			//fs_out.f_prior.thick = max(zfront_prior_f - zfront_posterior_f, 0); // to avoid computational precision error (0 or small minus)
			fs_out.f_prior.thick = zfront_prior_f - zfront_posterior_f;
			fs_out.f_prior.rayDist = zfront_prior_f;

#if LINEAR_MODE == 1
			{
				f_m_prior_vis = f_posterior_vis * (fs_out.f_prior.thick / f_posterior.thick);
			}
#else
			{
				float zd_ratio = fs_out.f_prior.thick / f_posterior.thick;
				float Ad = f_posterior_vis.a;
				float3 Id = f_posterior_vis.rgb / Ad;

				// strict mode
				float Az = Ad * zd_ratio * beta + (1 - beta) * (1 - pow(abs(1 - Ad), zd_ratio));
				// approx. mode
				//float term1 = zd_ratio * (1 - zd_ratio) / 2.f * Ad * Ad;
				//float term2 = term1 * (2 - zd_ratio) / 3.f * Ad;
				//float term3 = term2 * (3 - zd_ratio) / 4.f * Ad;
				//float term4 = term3 * (4 - zd_ratio) / 5.f * Ad;
				//float Az = Ad * zd_ratio + (1 - beta) * (term1 + term2 + term3 + term4 + term4 * (5 - zd_ratio) / 6.f * Ad);

				float3 Cz = Id * Az;
				f_m_prior_vis = float4(Cz, Az);
			}
#endif

			float old_alpha = f_posterior_vis.a;
			f_posterior.thick -= fs_out.f_prior.thick;
			f_posterior_vis = (f_posterior_vis - f_m_prior_vis) / (1.f - f_m_prior_vis.a);

			fs_out.f_prior.opacitySum = f_posterior.opacitySum * f_m_prior_vis.a / old_alpha;
			f_posterior.opacitySum = f_posterior.opacitySum - fs_out.f_prior.opacitySum;
		}

		// merge the fusion sub_rs (f_prior) to fs_out.f_prior
		fs_out.f_prior.thick += f_prior.thick;
		fs_out.f_prior.rayDist = f_prior.rayDist;
		float4 f_mid_vis = f_posterior_vis * (f_prior.thick / f_posterior.thick); // REDESIGN
		float f_mid_alphaw = f_posterior.opacitySum * f_mid_vis.a / f_posterior_vis.a;
		
		float4 f_mid_mix_vis = MixOpt(f_mid_vis, f_mid_alphaw, f_prior_vis, f_prior.opacitySum);
		f_m_prior_vis += f_mid_mix_vis * (1.f - f_m_prior_vis.a);
		fs_out.f_prior.opacitySum += f_mid_alphaw + f_prior.opacitySum;

		f_posterior.thick -= f_prior.thick;
		float old_alpha = f_posterior_vis.a;
		f_posterior_vis = (f_posterior_vis - f_mid_vis) / (1.f - f_mid_vis.a);
		f_posterior.opacitySum -= f_mid_alphaw;

		f_posterior.color = f_posterior_vis;
		fs_out.f_prior.color = f_m_prior_vis;

		if (f_posterior.color.a < 1.f/255.f)
		{
			f_posterior.color = (float4)0;
			f_posterior.thick = 0;
		}
		if (fs_out.f_prior.color.a < 1.f/255.f)
		{
			fs_out.f_prior.color = (float4)0;
			fs_out.f_prior.thick = 0;
		}
	}
	fs_out.f_posterior = f_posterior;
	return fs_out;
}
#endif

inline void IntermixSample(inout float4 colorIntegrated, inout Fragment f_next_layer, inout uint indexFrag, 
	const float4 color, const float rayDist, const float sampleThick, 
	const uint numFrags, const Fragment fs[MAX_FRAGMENTS], 
	const float mergingBeta) 
{
	// rayDist refers to the distance from the ray origin to the backside of the sample slab 
    if (indexFrag >= numFrags)
    {
        colorIntegrated += color * (1.f - colorIntegrated.a);
    }
    else
    {
        Fragment f_cur;
		/*conversion to integer causes some color difference.. but negligible*/
        f_cur.color = color;
#ifdef FRAG_MERGING
        f_cur.rayDist = rayDist;
        f_cur.thick = sampleThick;
		f_cur.opacitySum = color.a;
#endif
        [loop]
        while (indexFrag < numFrags && f_cur.color.a > 0)
        {
#ifdef FRAG_MERGING	
			if (f_cur.rayDist < f_next_layer.rayDist - f_next_layer.thick)
            {
				colorIntegrated += f_cur.color * (1.f - colorIntegrated.a);
				f_cur.color.a = 0;
                break;
            }
			else if (f_next_layer.rayDist < f_cur.rayDist - f_cur.thick)
            {
				colorIntegrated += f_next_layer.color * (1.f - colorIntegrated.a);
				f_next_layer = fs[++indexFrag];
            }
			else
			{
				Fragment f_prior, f_posterior; 
				if (f_cur.rayDist > f_next_layer.rayDist)
				{
					f_prior = f_next_layer; 
					f_posterior = f_cur; 
				}
				else
				{
					f_prior = f_cur; 
					f_posterior = f_next_layer; 
				}
				Fragment_OUT fs_out = MergeFrags(f_prior, f_posterior, mergingBeta);
				colorIntegrated += fs_out.f_prior.color * (1.f - colorIntegrated.a);
				if (f_cur.rayDist > f_next_layer.rayDist)
				{
					f_cur = fs_out.f_posterior;
					f_next_layer = fs[++indexFrag];
				}
				else
				{
					f_cur.color = (float4)0; 
					if (fs_out.f_posterior.color.a == 0)
					{
						f_next_layer = fs[++indexFrag]; 
					}
					else
					{
						f_next_layer = fs_out.f_posterior;
					}
					break;
				}
			}
            if (colorIntegrated.a > ERT_ALPHA)
            {
				//indexFrag = numFrags;
                break;
            }
#else
			float dist_diff = rayDist - f_next_layer.rayDist;
			float4 color_mix = color;
			if (dist_diff >= 0) 
			{
				if (dist_diff < sampleThick) {
					color_mix = MixOpt(color, color.a, f_next_layer.color, f_next_layer.color.a);
					/*is_sample_used = true;*/ 
				}
				else {
					color_mix = f_next_layer.color;
				}
				colorIntegrated += color_mix * (1.f - colorIntegrated.a);
				indexFrag++;
                if (colorIntegrated.a > ERT_ALPHA)
                {
                    break;
                }
			}
			else
			{
				colorIntegrated += color_mix * (1.f - colorIntegrated.a);
				/*is_sample_used = true;*/
				break;
			}
#endif // FRAG_MERGING
        } // while (indexFrag < numFrags && f_cur.color.a > 0)
#ifdef FRAG_MERGING
		if (f_cur.color.a > 0)
		{
			colorIntegrated += f_cur.color * (1.f - colorIntegrated.a);
		}
#endif
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