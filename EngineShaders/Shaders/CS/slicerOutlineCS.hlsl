#include "../Globals.hlsli"
#include "../CommonHF/objectRayHF.hlsli"
#define K_NUM 2
#include "../CommonHF/kbufferHF.hlsli"

// Use the same naming convention to meshSlicerCS.hlsl
RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<uint> counter_mask_distmap : register(u1);
RWTexture2D<uint4> layer_packed0_RGBA : register(u2);
RWTexture2D<uint2> layer_packed1_RG : register(u3);

// value:  [-d, d] ������ �Է� ��
// d:      �ִ� ����(��/��)
// ��ȯ��: �ش� value�� ���ε� RGBA ����
//float4 ColorMapRange(float value, float d)
//{
//	// 1) [-d, d] ������ [0, 1] ������ ����ȭ
//	//    value = -d �� �� 0, value = d �� �� 1�� �ǵ��� ����
//	float t = (value + d) / (2.0 * d);
//
//	// 2) 0 ���ϳ� 1 �̻����� �Ѿ�� ��츦 ����� saturate ���
//	t = saturate(t);
//
//	// 3) ���ϴ� ���� ���� �� �� ���� (��: �Ķ��� -> ������)
//	float3 colorMin = float3(0.0, 1.0, 1.0);  // �Ķ���
//	float3 colorMax = float3(1.0, 1.0, 0.0);  // ������
//
//	// 4) lerp�� ���� ����
//	float3 color = lerp(colorMin, colorMax, t);
//
//	// ���� RGBA ��ȯ (���Ĵ� 1�� ����)
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