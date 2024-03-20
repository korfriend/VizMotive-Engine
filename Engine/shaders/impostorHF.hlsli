#ifndef VZ_IMPOSTOR_HF
#define VZ_IMPOSTOR_HF

struct VSOut
{
	precise float4 pos				: SV_Position;
	float clip						: SV_ClipDistance0;
	float2 uv						: TEXCOORD;
	uint slice						: SLICE;
	nointerpolation float dither	: DITHER;
	float3 pos3D					: WORLDPOSITION;
	uint instanceColor				: COLOR;
	uint primitiveID				: PRIMITIVEID;
};

Texture2DArray<float4> impostorTex : register(t1);

#endif // VZ_IMPOSTOR_HF
