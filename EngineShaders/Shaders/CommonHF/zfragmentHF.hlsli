#include "../Globals.hlsli"

#define SAFE_OPAQUEALPHA_HALF 	(half)0.99f		// ERT_ALPHA

inline uint pack_R11G11B10_rgba8(in float3 rgb) {
	uint retVal = 0;
	retVal |= (uint)(clamp(rgb.r, 0.0, 1.0) * 255.0 + 0.5) << 0u;
	retVal |= (uint)(clamp(rgb.g, 0.0, 1.0) * 255.0 + 0.5) << 8u;
	retVal |= (uint)(clamp(rgb.b, 0.0, 1.0) * 255.0 + 0.5) << 16u;
	retVal |= 255u << 24u; // Alpha는 1(255)로 설정
	return retVal;
}

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
	inline half GetZ_Half() { return (half)z; }
	void Init()
	{
		color_packed = 0;
		zthick = opacity_sum = 0;
		z = FLT_MAX;
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

void Fragment_OrderIndependentMerge(inout Fragment f_buf, const in Fragment f_in)
{
	half4 f_buf_color = f_buf.GetColor();
	half4 f_in_color = f_in.GetColor();

	half4 f_mix_vis = MixOpt(f_buf_color, f_buf.opacity_sum, f_in_color, f_in.opacity_sum);
	f_buf.SetColor(f_mix_vis);
	f_buf.opacity_sum = f_buf.opacity_sum + f_in.opacity_sum;
	float z_front = min(f_buf.z - (float)f_buf.zthick, f_in.z - (float)f_in.zthick);
	f_buf.z = max(f_buf.z, f_in.z);
	f_buf.zthick = (half)(f_buf.z - z_front);
}


inline bool OverlapTest(const in Fragment f_1, const in Fragment f_2)
{
	float diff_z1 = f_1.z - (f_2.z - (float)f_2.zthick);
	float diff_z2 = (f_1.z - (float)f_1.zthick) - f_2.z;
	return diff_z1 * diff_z2 < 0;
}

// refer to Algorithm 1 in https://onlinelibrary.wiley.com/doi/full/10.1111/cgf.14409
int OverlapFragments(Fragment f_prior, Fragment f_posterior, out Fragment f_prior_out, out Fragment f_posterior_out)
{
	// Overall algorithm computation cost 
	// : 3 branches, 2 visibility interpolations, 2 visibility integrations, and 1 fusion of overlapping ray-segments

	// f_prior and f_posterior mean f_prior.z >= f_prior.z
	Fragment f_out = (Fragment)0;

	float zfront_posterior_f = f_posterior.z - (float)f_posterior.zthick;
	int case_ret = 0;
	// f_prior and f_posterior mean f_prior.z >= f_prior.z

	if (f_prior.z <= zfront_posterior_f)
	{
		// Case 1 : No Overlapping
		f_prior_out = f_prior;
		case_ret = 1;
	}
	else // if (f_prior.z > zfront_posterior_f) // overlapping test
	{
		Fragment f_m_prior;
		half4 f_m_prior_vis;
		half4 f_prior_vis = f_prior.GetColor();
		half4 f_posterior_vis = f_posterior.GetColor();
		f_prior_vis.a = min(f_prior_vis.a, SAFE_OPAQUEALPHA_HALF);
		f_posterior_vis.a = min(f_posterior_vis.a, SAFE_OPAQUEALPHA_HALF);

		float zfront_prior_f = f_prior.z - (float)f_prior.zthick;
		if (zfront_prior_f < zfront_posterior_f)
		{
			// Case 2 : Intersecting each other
			f_m_prior.zthick = (half)(zfront_posterior_f - zfront_prior_f);
			f_m_prior.z = zfront_posterior_f;
			{
				f_m_prior_vis = f_prior_vis * (f_m_prior.zthick / f_prior.zthick);
			}
			half old_alpha = f_prior_vis.a;
			f_prior.zthick -= f_m_prior.zthick;
			f_prior_vis = (f_prior_vis - f_m_prior_vis) / ((half)1.f - f_m_prior_vis.a);

			f_m_prior.opacity_sum = f_prior.opacity_sum * f_m_prior_vis.a / old_alpha;
			f_prior.opacity_sum = f_prior.opacity_sum * f_prior_vis.a / old_alpha;
			case_ret = 2;
		}
		else
		{
			// Case 3 : f_prior belongs to f_posterior
			f_m_prior.zthick = half(zfront_prior_f - zfront_posterior_f);
			f_m_prior.z = zfront_prior_f;
			{
				f_m_prior_vis = f_posterior_vis * (f_m_prior.zthick / f_posterior.zthick);
			}

			half old_alpha = f_posterior_vis.a;
			f_posterior.zthick -= f_m_prior.zthick;
			f_posterior_vis = (f_posterior_vis - f_m_prior_vis) / ((half)1.f - f_m_prior_vis.a);

			f_m_prior.opacity_sum = f_posterior.opacity_sum * f_m_prior_vis.a / old_alpha;
			f_posterior.opacity_sum = f_posterior.opacity_sum * f_posterior_vis.a / old_alpha;
			case_ret = 3;
		}

		// merge the fusion (remained) f_prior to f_m_prior
		f_m_prior.zthick += f_prior.zthick;
		f_m_prior.z = f_prior.z;
		half4 f_mid_vis = f_posterior_vis * (f_prior.zthick / f_posterior.zthick); // REDESIGN
		half f_mid_alphaw = f_posterior.opacity_sum * f_mid_vis.a / f_posterior_vis.a;

		half4 f_mid_mix_vis = MixOpt(f_mid_vis, f_mid_alphaw, f_prior_vis, f_prior.opacity_sum);
		f_m_prior_vis += f_mid_mix_vis * ((half)1.f - f_m_prior_vis.a); // OV operator
		f_m_prior.opacity_sum += f_mid_alphaw + f_prior.opacity_sum;

		f_posterior.zthick -= f_prior.zthick;
		half old_alpha = f_posterior_vis.a;
		f_posterior_vis = (f_posterior_vis - f_mid_vis) / ((half)1.f - f_mid_vis.a);
		//f_posterior.opacity_sum -= f_mid_alphaw;
		f_posterior.opacity_sum *= f_posterior_vis.a / old_alpha;


		// ---------------
		f_posterior.SetColor(f_posterior_vis);
		f_prior_out.SetColor(f_m_prior_vis);

		if (f_posterior_vis.a < (half)(1.f / 255.f))
		{
			f_posterior.color_packed = 0;
			f_posterior.zthick = 0;
		}
		if (f_m_prior_vis.a < (half)(1.f / 255.f))
		{
			f_posterior.color_packed = 0;
			f_posterior.zthick = 0;
		}
	}
	f_posterior_out = f_posterior;

	return case_ret;
}

Fragment MergeFragments(Fragment f_prior, Fragment f_posterior)
{
	Fragment f_prior_out, f_posterior_out, f_out;
	OverlapFragments(f_prior, f_posterior, f_prior_out, f_posterior_out);

	half4 vis0 = f_prior_out.GetColor();
	half4 vis1 = f_posterior_out.GetColor();
	half4 vis_out = vis0 + vis1 * ((half)1.f - vis0.a);
	if (vis_out.a > (half)(1.f / 255.f))
	{
		f_out.SetColor(vis_out);
		f_out.zthick = f_prior_out.zthick + f_posterior_out.zthick;
		f_out.z = f_posterior_out.z;
		f_out.opacity_sum = f_prior_out.opacity_sum + f_posterior_out.opacity_sum;
	}
	else
	{
		f_out.Init();
	}

	return f_out;
}

// NOTE THAT current low-res GPUs shows unexpected behavior when using sort_insert. 
// Instead, Do USE sort_insertOpt and sort_shellOpt
#define SORT_INSERT(num, fragments, FRAG) {					\
	[loop]												\
	for (uint j = 1; j < num; ++j)						\
	{													\
		FRAG key = fragments[j];					\
		uint i = j - 1;									\
														\
		[loop]											\
		while (i >= 0 && fragments[i].z > key.z)\
		{												\
			fragments[i + 1] = fragments[i];			\
			--i;										\
		}												\
		fragments[i + 1] = key;							\
	}													\
}


#define SORT_INSERTOPT(num, fragments, FRAG) {			\
	[loop]												\
	for (uint j = 1; j < num; ++j)						\
	{													\
		FRAG key = fragments[j];						\
		uint i = j - 1;									\
		FRAG df = fragments[i];							\
		[loop]											\
		while (i >= 0 && df.z > key.z)					\
		{												\
			fragments[i + 1] = df;						\
			--i;										\
			df = fragments[i];							\
		}												\
		fragments[i + 1] = key;							\
	}													\
}


#define SORT_SHELL(num, fragments, FRAG) {								\
	uint inc = num >> 1;												\
	[loop]															\
	while (inc > 0)													\
	{																\
		[loop]														\
		for (uint i = inc; i < num; ++i)								\
		{															\
			FRAG tmp = fragments[i];							\
																	\
			uint j = i;												\
			[loop]													\
			while (j >= inc && fragments[j - inc].z > tmp.z)		\
			{														\
				fragments[j] = fragments[j - inc];					\
				j -= inc;											\
			}														\
			fragments[j] = tmp;										\
		}															\
		inc = uint(inc / 2.2f + 0.5f);								\
	}																\
}


#define SORT_SHELLOPT(num, fragments, FRAG) {								\
	uint inc = num >> 1;												\
	[loop]															\
	while (inc > 0)													\
	{																\
		[loop]														\
		for (uint i = inc; i < num; ++i)								\
		{															\
			FRAG tmp = fragments[i];							\
																	\
			uint j = i;												\
			FRAG dfrag = fragments[j - inc];						\
			[loop]													\
			while (j >= inc && dfrag.z > tmp.z)						\
			{														\
				fragments[j] = fragments[j - inc];					\
				j -= inc;											\
				dfrag = fragments[j - inc];							\
			}														\
			fragments[j] = tmp;										\
		}															\
		inc = uint(inc / 2.2f + 0.5f);								\
	}																\
}

#define MERGE(steps, a, b, c) {														 \
	uint i;																			 \
	[loop]																			 \
	for (i = 0; i < steps; ++i)														 \
		leftArray[i] = fragments[a + i];											 \
																					 \
	i = 0;																			 \
	uint j = 0;																		 \
	[loop]																			 \
	for (uint k = a; k < c; ++k)														 \
	{																				 \
		if (b + j >= c || (i < steps && leftArray[i].z < fragments[b + j].z))\
			fragments[k] = leftArray[i++];											 \
		else																		 \
			fragments[k] = fragments[b + j++];										 \
	}																				 \
}

#define SORT_MERGE(num, fragments, FRAG, SIZE_2D){								  \
	FRAG leftArray[SIZE_2D];					  \
	uint n = num;												  \
	uint steps = 1;												  \
																  \
	[loop]														  \
	while (steps <= n)											  \
	{															  \
		uint i = 0;												  \
		[loop]													  \
		while (i < n - steps)									  \
		{														  \
			MERGE(steps, i, i + steps, min(i + steps + steps, n));\
			i += (steps << 1); /*i += 2 * steps;*/				  \
		}														  \
		steps = (steps << 1); /*steps *= 2;	*/					  \
	}															  \
}

#define SORT(num, fragments, FRAG) {	   \
	if (num <= 16)				   \
		SORT_INSERTOPT(num, fragments, FRAG)\
	else						   \
		SORT_SHELLOPT(num, fragments, FRAG)\
}