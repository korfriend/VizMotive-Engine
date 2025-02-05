#include "../Globals.hlsli"

PUSHCONSTANT(push, WetmapPush);

[numthreads(64, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
	ShaderMeshInstance meshinstance = load_instance(push.instanceIndex);
	ShaderGeometry geometry = load_geometry(meshinstance.geometryOffset + push.subsetIndex);
	if(geometry.vb_pos_w < 0)
		return;

	Buffer<float4> vb_pos_w = bindless_buffers_float4[descriptor_index(geometry.vb_pos_w)];
	float3 world_pos = vb_pos_w[DTid].xyz;
	world_pos = mul(meshinstance.transform.GetMatrix(), float4(world_pos, 1)).xyz;

	float drying = 0.02;
	[branch]
	if(geometry.vb_nor >= 0)
	{
		Buffer<half4> vb_nor = bindless_buffers_half4[descriptor_index(geometry.vb_nor)];
		float3 normal = vb_nor[DTid].xyz;
		normal = normalize(normal);
		drying *= lerp(4, 1, pow(saturate(normal.y), 8)); // modulate drying speed based on surface slope
	}
		
	RWBuffer<float> wetmap = bindless_rwbuffers_float[descriptor_index(push.wetmap)];

	float prev = wetmap[DTid];
	float current = prev;

	bool drying_enabled = false;//true;

	// this is for rain-blocking field (using shadow data structures)
	// we do not use the rain-blocking simulation for controlling wetmap behavior...	
	// so, just ignore!

	//if(push.wet_amount > 0 && GetFrame().texture_shadowatlas_index >= 0)
	//{
	//	Texture2D texture_shadowatlas = bindless_textures[GetFrame().texture_shadowatlas_index];
	//	float3 shadow_pos = mul(GetFrame().rain_blocker_matrix, float4(world_pos, 1)).xyz;
	//	float3 shadow_uv = clipspace_to_uv(shadow_pos);
	//	float shadow = 1;
	//	if (is_saturated(shadow_uv))
	//	{
	//		shadow_uv.xy = mad(shadow_uv.xy, GetFrame().rain_blocker_mad.xy, GetFrame().rain_blocker_mad.zw);
	//
	//		float cmp = shadow_pos.z + 0.001;
	//		shadow  = texture_shadowatlas.SampleCmpLevelZero(sampler_cmp_depth, shadow_uv.xy, cmp, 2 * int2(-1, -1)).r;
	//		shadow += texture_shadowatlas.SampleCmpLevelZero(sampler_cmp_depth, shadow_uv.xy, cmp, 2 * int2(-1, 0)).r;
	//		shadow += texture_shadowatlas.SampleCmpLevelZero(sampler_cmp_depth, shadow_uv.xy, cmp, 2 * int2(-1, 1)).r;
	//		shadow += texture_shadowatlas.SampleCmpLevelZero(sampler_cmp_depth, shadow_uv.xy, cmp, 2 * int2(0, -1)).r;
	//		shadow += texture_shadowatlas.SampleCmpLevelZero(sampler_cmp_depth, shadow_uv.xy, cmp, 2 * int2(0, 1)).r;
	//		shadow += texture_shadowatlas.SampleCmpLevelZero(sampler_cmp_depth, shadow_uv.xy, cmp, 2 * int2(1, -1)).r;
	//		shadow += texture_shadowatlas.SampleCmpLevelZero(sampler_cmp_depth, shadow_uv.xy, cmp, 2 * int2(1, 0)).r;
	//		shadow += texture_shadowatlas.SampleCmpLevelZero(sampler_cmp_depth, shadow_uv.xy, cmp, 2 * int2(1, 1)).r;
	//		shadow /= 9.0;
	//	}
	//
	//	if(shadow == 0)
	//		drying_enabled = false;
	//	
	//	current = lerp(current, smoothstep(0, 1.4, sqrt(shadow)), saturate(GetDeltaTime() * 0.5));
	//}

	if(drying_enabled)
		current = lerp(current, 0, GetDeltaTime() * drying);
	
	wetmap[DTid] = current;
}
