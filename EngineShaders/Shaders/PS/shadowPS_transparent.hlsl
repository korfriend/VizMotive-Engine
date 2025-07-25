#define OBJECTSHADER_LAYOUT_SHADOW_TEX
#define OBJECTSHADER_USE_COLOR
#include "../CommonHF/objectHF.hlsli"

[earlydepthstencil]
float4 main(PixelInput input) : SV_TARGET
{
	ShaderMaterial material = GetMaterial();

	float4 uvsets = input.GetUVSets();
	half4 color;
	[branch]
	if (material.textures[BASECOLORMAP].IsValid())
	{
		color = (half)material.textures[BASECOLORMAP].Sample(sampler_objectshader, uvsets);
	}
	else
	{
		color = 1;
	}
	
	[branch]
	if (material.textures[TRANSPARENCYMAP].IsValid())
	{
		color.a *= (half)material.textures[TRANSPARENCYMAP].Sample(sampler_objectshader, uvsets).r;
	}
	
	color *= input.color;
	
	ShaderMeshInstance meshinstance = load_instance(input.GetInstanceIndex());

	clip(color.a - material.GetAlphaTest() - meshinstance.GetAlphaTest());

	half opacity = color.a;
	
	half transmission = lerp(material.GetTransmission(), 1, material.GetCloak());
	color.rgb = lerp(color.rgb, 1, material.GetCloak());

	[branch]
	if (transmission > 0)
	{
		[branch]
		if (material.textures[TRANSMISSIONMAP].IsValid())
		{
			half transmissionMap = (half)material.textures[TRANSMISSIONMAP].Sample(sampler_objectshader, uvsets).r;
			transmission *= transmissionMap;
		}
		opacity *= 1 - transmission;
	}
	
	opacity = lerp(opacity, 0.5, material.GetCloak());

	color.rgb *= 1 - opacity; // if fully opaque, then black (not let through any light)

	color.a = (half)input.pos.z; // secondary depth

	return color;
}
