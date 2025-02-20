#include "../Globals.hlsli"

struct Fragment
{
	uint color_packed;
	float z;
	half zthick;
	half opacity_sum;

	inline half4 GetColor() { return unpack_rgba(color_packed); }
	inline void SetColor(half4 color) { color_packed = pack_rgba(color); }
	inline uint Pack_Zthick_AlphaSum() { return pack_half2(zthick, opacity_sum); }
	inline void Unpack_Zthick_AlphaSum(in uint value) { 
		zthick = (half)f16tof32(value.x);
		opacity_sum = (half)f16tof32(value.x >> 16u);
	}
};

half4 MixOpt(const in half4 vis1, const in half alphaw1, const in half4 vis2, const in half alphaw2)
{
	half4 vout = (half4)0;
	if (alphaw1 + alphaw2 > 0)
	{
		half3 C_mix1 = vis1.rgb / vis1.a * alphaw1;
		half3 C_mix2 = vis2.rgb / vis2.a * alphaw2;
		half3 I_mix = (C_mix1 + C_mix2) / (alphaw1 + alphaw2);
		half T_mix1 = 1 - vis1.a;
		half T_mix2 = 1 - vis2.a;
		half A_mix = 1 - T_mix1 * T_mix2;
		vout = half4(I_mix * A_mix, A_mix);
	}
	return vout;
}