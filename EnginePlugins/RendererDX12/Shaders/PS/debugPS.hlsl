#include "../CommonHF/imageHF.hlsli"

#define PRIMITIVE_TEXTURE 1

float3 HueToRGB(float hue) {
    float r = abs(hue * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(hue * 6.0 - 2.0);
    float b = 2.0 - abs(hue * 6.0 - 4.0);
    return saturate(float3(r, g, b));
}

float3 IntToRGB_UNORM(uint value) {
    float hue = float(value % 360) / 360.0; // [0, 1] 범위로 Hue 값 정규화
    return HueToRGB(hue);
}

float3 IntToRGB_UNORM2(uint value) {
    // 해싱으로 유니크한 값 생성
    value ^= (value >> 21);
    value ^= (value << 35);
    value ^= (value >> 4);
    value = value % 16777216;

    // 비선형 변환 (사인, 코사인, 제곱근)
    float normalized = float(value) / 16777215.0;
    float r = sin(normalized * 6.2831) * 0.5 + 0.5;
    float g = cos(normalized * 6.2831) * 0.5 + 0.5;
    float b = sqrt(normalized);

    return float3(r, g, b);
}

float4 main(VertextoPixel input) : SV_TARGET
{
	half4 color = unpack_half4(image.packed_color);
	//float4 uvsets = input.compute_uvs();

	Texture2D texture = bindless_textures[image.texture_base_index];

	// in this debug mode, sampler_index is used for specifying the debug buffer
	if (image.sampler_index == PRIMITIVE_TEXTURE)
	{
		uint primitive_id = asuint(texture.Load(int3(input.pos.xy, 0)).r);
		if (primitive_id > 0)
		{
			color.rgb = IntToRGB_UNORM2(primitive_id);
			color.a = 1.f;
		}
		else{
			color.rgb = float3(0, 0, 0);
		}
	}

	return color;
}
