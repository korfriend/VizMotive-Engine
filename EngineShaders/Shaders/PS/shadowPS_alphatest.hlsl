#define OBJECTSHADER_LAYOUT_SHADOW_TEX
#include "../CommonHF/objectHF.hlsli"


void main(PixelInput input)
{
	ShaderMaterial material = GetMaterial();
	
	half alpha = 1;

	[branch]
	if (material.textures[BASECOLORMAP].IsValid())
	{
		alpha = (half)material.textures[BASECOLORMAP].Sample(sampler_point_wrap, input.GetUVSets()).a;
	}
	
	[branch]
	if (material.textures[TRANSPARENCYMAP].IsValid())
	{
		alpha *= (half)material.textures[TRANSPARENCYMAP].Sample(sampler_point_wrap, input.GetUVSets()).r;
	}
	
	ShaderMeshInstance meshinstance = load_instance(input.GetInstanceIndex());
	
	clip(alpha - material.GetAlphaTest() - meshinstance.GetAlphaTest());
}
