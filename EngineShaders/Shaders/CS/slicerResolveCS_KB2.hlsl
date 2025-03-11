#include "../Globals.hlsli"
#include "../CommonHF/objectRayHF.hlsli"
#define K_NUM 2
#include "../CommonHF/kbufferHF.hlsli"

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<uint> counter_mask_distmap : register(u1);
RWTexture2D<uint4> layer_packed0_RGBA : register(u2);
RWTexture2D<uint2> layer_packed1_RG : register(u3);

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
	uint frag_cnt = c_m_d & 0xFF;
	uint mask = (c_m_d >> 8) & 0xFF;

	if (frag_cnt == 0)
	{
		return;
	}

	if (mask & (SLICER_DEPTH_OUTLINE_DIRTY | SLICER_DEPTH_OUTLINE))
	{
		return;
	}

	uint4 v_layer_packed0_RGBA = layer_packed0_RGBA[pixel];
	uint2 v_layer_packed1_RG = layer_packed1_RG[pixel];

	Fragment f_0;
	f_0.Unpack_8bitUIntRGBA(v_layer_packed0_RGBA.r);
	f_0.z = asfloat(v_layer_packed0_RGBA.g);
	f_0.Unpack_Zthick_AlphaSum(v_layer_packed0_RGBA.b);

	Fragment f_1;
	f_1.Unpack_8bitUIntRGBA(v_layer_packed0_RGBA.a);
	f_1.z = asfloat(v_layer_packed1_RG.r);
	f_1.Unpack_Zthick_AlphaSum(v_layer_packed1_RG.g);

	Fragment fs[2] = { f_0, f_1 };

	uint final_frag_count = 0;
	half4 color_out = Resolve_kBuffer(frag_cnt, fs, final_frag_count);

    inout_color[pixel] = color_out;
}