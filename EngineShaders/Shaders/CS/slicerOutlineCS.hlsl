#include "../Globals.hlsli"
#include "../CommonHF/objectRayHF.hlsli"
#include "../CommonHF/zfragmentHF.hlsli"

// Use the same naming convention to meshSlicerCS.hlsl
RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<uint> layer1_thick_asum : register(u1);

Texture2D<float> distance_map : register(t0);

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

	float sd = distance_map[pixel];
	uint wildcard_v = asuint(sd);
	if (wildcard_v == WILDCARD_DEPTH_OUTLINE || wildcard_v == OUTSIDE_PLANE)
		return;

#define MIN_OUTLINE_PIXEL 2.f

	const float lineThres = max(push.outlineThickness, MIN_OUTLINE_PIXEL);
	float distAbs = abs(sd);
	if (distAbs >= lineThres)
		return;

	float a = max(min(lineThres - distAbs, MIN_OUTLINE_PIXEL), 0); // AA option
	
	if (a <= 0.01) {
		return;
	}
	
	ShaderMaterial material = GetMaterial();
	half4 base_color = material.GetBaseColor();

	float4 outline_color = float4(base_color.xyz, a); // a*a // heuristic aliasing 
	outline_color.rgb *= outline_color.a;

	bool disabled_filling = push.sliceFlags & SLICER_FLAG_ONLY_OUTLINE;

	inout_color[pixel] = outline_color;
	if (disabled_filling) {
		//Fill_kBuffer(ss_xy, g_cbCamState.k_value, outline_color, 0.0001, max(g_cbCamState.far_plane, 0.1));

	}

	layer1_thick_asum[pixel] = WILDCARD_DEPTH_OUTLINE_DIRTY;
}