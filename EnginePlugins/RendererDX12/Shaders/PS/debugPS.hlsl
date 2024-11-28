#include "../CommonHF/imageHF.hlsli"

#define PRIMITIVE_ID 1
#define INSTANCE_ID 2
#define LINEAR_DEPTH 3

uint ImprovedHash(uint value) {
    value ^= (value >> 21);
    value *= 2654435761u; // Knuth's Multiplicative Hash
    value ^= (value << 13);
    return value;
}

float3 IntToRGB_UNORM(uint value) {
    // 향상된 해시
    value = ImprovedHash(value) % 16777216;

    // 정규화된 범위 [0, 1]로 변환
    float normalized = float(value) / 16777215.0;

    // Hue 기반 색상 변환
    float hue = normalized; // [0, 1] 범위
    float r = abs(hue * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(hue * 6.0 - 2.0);
    float b = 2.0 - abs(hue * 6.0 - 4.0);

    return saturate(float3(r, g, b)); // RGB 값 제한
}

float3 HueToRGB(float hue) {
    // Convert hue (0~1 range) to RGB
    float r = abs(hue * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(hue * 6.0 - 2.0);
    float b = 2.0 - abs(hue * 6.0 - 4.0);
    return saturate(float3(r, g, b)); // Clamp to [0, 1]
}

float3 IntToRGB_UNORM2(uint value) {
    // Ensure a unique hue for 0~100 range
    const float golden_ratio = 0.61803398875; // Golden ratio constant
    float hue = frac(value * golden_ratio);  // Unique hue using golden ratio
    return HueToRGB(hue); // Convert to RGB
}

float4 main(VertextoPixel input) : SV_TARGET
{
	//half4 color = unpack_half4(image.packed_color);
	float4 color = float4(0, 0, 0, 1);

	Texture2D texture = bindless_textures[image.texture_base_index];

	// in this debug mode, sampler_index is used for specifying the debug buffer
	switch (image.sampler_index)
	{
		case PRIMITIVE_ID:
		{
			uint primitive_id = asuint(texture.Load(int3(input.pos.xy, 0)).r);
			if (primitive_id > 0) {
				color.rgb = IntToRGB_UNORM2(primitive_id - 1);
			}
		} break;
		case INSTANCE_ID:
		{
			uint instance_id = asuint(texture.Load(int3(input.pos.xy, 0)).r);
			
			uint instance_index = instance_id & 0xFFFFFF;
			uint part_index = (instance_id >> 24u) & 0xFF;

			if (instance_id > 0) {
				color.rgb = IntToRGB_UNORM2((instance_index - 1) * 100 + part_index);
			}
		} break;
		case LINEAR_DEPTH:
		{
			float linear_depth = texture.Load(int3(input.pos.xy, 0)).r;
			color = float4((float3)linear_depth * 8.f, 1);
		} break;
	}	

	return color;
}
