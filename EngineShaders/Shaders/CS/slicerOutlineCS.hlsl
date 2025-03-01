#include "../Globals.hlsli"
#include "../CommonHF/objectRayHF.hlsli"
#define K_NUM 2
#include "../CommonHF/kbufferHF.hlsli"

// Use the same naming convention to meshSlicerCS.hlsl
RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<uint> counter_mask_distmap : register(u1);
RWTexture2D<uint4> layer_packed0_RGBA : register(u2);
RWTexture2D<uint2> layer_packed1_RG : register(u3);

// value:  [-d, d] 범위의 입력 값
// d:      최대 범위(양/음)
// 반환값: 해당 value에 매핑된 RGBA 색상
//float4 ColorMapRange(float value, float d)
//{
//	// 1) [-d, d] 범위를 [0, 1] 범위로 정규화
//	//    value = -d 일 때 0, value = d 일 때 1이 되도록 매핑
//	float t = (value + d) / (2.0 * d);
//
//	// 2) 0 이하나 1 이상으로 넘어가는 경우를 대비해 saturate 사용
//	t = saturate(t);
//
//	// 3) 원하는 시작 색과 끝 색 정의 (예: 파란색 -> 빨간색)
//	float3 colorMin = float3(0.0, 1.0, 1.0);  // 파란색
//	float3 colorMax = float3(1.0, 1.0, 0.0);  // 빨간색
//
//	// 4) lerp로 선형 보간
//	float3 color = lerp(colorMin, colorMax, t);
//
//	// 최종 RGBA 반환 (알파는 1로 설정)
//	return float4(color, 1.0);
//}

[numthreads(VISIBILITY_BLOCKSIZE, VISIBILITY_BLOCKSIZE, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
	ShaderCamera camera = GetCamera();

	const uint2 pixel = DTid.xy;
	const bool pixel_valid = (pixel.x < camera.internal_resolution.x) && (pixel.y < camera.internal_resolution.y);
	if (!pixel_valid)
	{
		return;
	}

	uint c_m_d = counter_mask_distmap[pixel];
	uint count = c_m_d & 0xFF;
	uint mask = (c_m_d >> 8) & 0xFF;

	if (mask & (SLICER_DEPTH_OUTLINE | SLICER_OUTSIDE_PLANE | SLICER_DEBUG))
		return;

#define MIN_OUTLINE_PIXEL 1.3f

	const float lineThres = max(push.outlineThickness, MIN_OUTLINE_PIXEL);

	float sd = f16tof32(c_m_d >> 16);
	float distAbs = abs(sd);
	if (distAbs >= lineThres)
		return;

	float a = max(min(lineThres - distAbs, MIN_OUTLINE_PIXEL), 0); // AA option
	
	if (a <= 0.01) {
		return;
	}
	
	ShaderMaterial material = GetMaterial();
	half4 base_color = material.GetBaseColor();

	half4 outline_color = half4(base_color.xyz, a); // a*a // heuristic aliasing 
	outline_color.rgb *= outline_color.a;

	inout_color[pixel] = (float4)outline_color;

	counter_mask_distmap[pixel] = SLICER_DEPTH_OUTLINE_DIRTY << 8 | 1;
}