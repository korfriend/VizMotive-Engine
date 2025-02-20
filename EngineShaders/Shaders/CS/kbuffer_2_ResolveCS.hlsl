#include "../Globals.hlsli"
#define K_NUM 2
#include "../CommonHF/kbufferHF.hlsli"

RWTexture2D<unorm float4> inout_color : register(u0);
RWTexture2D<uint> layer0_color : register(u1); // RGBA
RWTexture2D<uint> layer1_color : register(u2);
RWTexture2D<float> layer0_depth : register(u3);
RWTexture2D<float> layer1_depth : register(u4); // pixel-space distance_map
RWTexture2D<uint> layer0_thick_asum : register(u5); // RG
RWTexture2D<uint> layer1_thick_asum : register(u6);

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

	uint color0_packed = layer0_color[pixel];
	if (color0_packed == 0)
	{
		return;
	}
	uint color1_packed = layer1_color[pixel];
	uint frag_cnt = color1_packed == 0 ? 1 : 2;

	Fragment f_0;
	f_0.color_packed = color0_packed;
	f_0.z = layer0_depth[pixel];
	f_0.Unpack_Zthick_AlphaSum(layer0_thick_asum[pixel]);

	Fragment f_1;
	f_1.color_packed = color1_packed;
	f_1.z = layer1_depth[pixel];
	f_1.Unpack_Zthick_AlphaSum(layer1_thick_asum[pixel]);
	Fragment fs[2] = { f_0, f_1 };

	uint final_frag_count = 0;
	half4 color_out = Resolve_kBuffer(frag_cnt, fs, final_frag_count);
	
    inout_color[pixel] = color_out;
}