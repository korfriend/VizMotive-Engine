#include "../Globals.hlsli"
#include "zfragmentHF.hlsli"

#define K_CORES K_NUM - 1
uint Fill_kBuffer(in Fragment f_in, in uint fragCount, inout Fragment fs[K_NUM])
{
	half4 color_in = f_in.color;

	if (f_in.z > 1e20 || color_in.a <= 0.01) return fragCount;

	int store_index = -1;
	int core_max_idx = -1;
	bool is_overflow = true;
	
	Fragment f_coremax, f_tail;
	f_coremax.Init();
	f_tail.Init();

	uint final_frag_count = fragCount;

	if (fragCount == K_NUM)
	{
		f_tail = fs[K_NUM - 1];
	}

	if (f_in.z > f_tail.z - (float)f_tail.zthick)
	{
		// fragCount == K_NUM case
		// this routine implies that f_tail.i_vis > 0 and f_tail.z is finite
		// update the merging slot
		Fragment_OrderIndependentMerge(f_in, f_tail);
		store_index = K_NUM - 1;
	}
	else
	{
		[loop]
		for (uint i = 0; i < K_CORES; i++)
		{
			Fragment f_i = (Fragment)0;
			if (i < fragCount)
			{
				f_i = fs[i];
			}

			if (f_i.color.a < SAFE_MIN_HALF) // empty slot by being 1) merged out or 2) not yet filled
			{
				store_index = i;
				core_max_idx = -2;
				is_overflow = false;
				break; // finish
			}
			else // if (f_i.color.a >= SAFE_MIN_HALF)
			{
				if (OverlapTest(f_in, f_i))
				{
					Fragment_OrderIndependentMerge(f_in, f_i);
					store_index = i;
					core_max_idx = -3;
					break; // finish
				}
				else if (f_coremax.z < f_i.z)
				{
					f_coremax = f_i;
					core_max_idx = i;
				}

				if (i == K_CORES - 1)
				{
					// core_max_idx >= 0
					// replacing or tail merging case 
					is_overflow = fragCount >= K_NUM;

					if (f_coremax.z > f_in.z) // replace case
					{
						// f_in to the core (and f_coremax to the tail when tail handling mode)
						store_index = core_max_idx;
					}
					else
					{
						// f_in to the tail and no to the core
						f_coremax = f_in;
					}

					if (!is_overflow) // implying fragCount < K_NUM
					{
						f_tail = f_coremax;
					}
					else
					{
						Fragment_OrderIndependentMerge(f_tail, f_coremax);
					}
				}
			}
		}
	}

	if (store_index >= 0)
	{
		fs[store_index] = f_in;
	}

	if (core_max_idx >= 0) // replace
	{
		fs[K_NUM - 1] = f_tail;
	}

	if (!is_overflow)
	{
		final_frag_count++;
	}
	return final_frag_count;
}

half4 Resolve_kBuffer(in uint fragCount, inout Fragment fs[K_NUM], out uint finalFragCount)
{
	//fragCount = min(fragCount, K_NUM);
	SORT(fragCount, fs, Fragment);

	// extended merging for consistent visibility transition
	//[loop]
	//for (uint k = 0; k < fragCount; k++)
	//{
	//	Fragment f_k = fs[k];
	//	if (f_k.i_vis != 0)
	//	{
	//		// optional setting for manual z-thickness
	//		f_k.zthick = max(f_k.zthick, GetVZThickness(f_k.z, vz_thickness));
	//		fs[k] = f_k;
	//	}
	//}

	half4 color_out = (half4)0;
	finalFragCount = 0;

	Fragment f_1, f_2;
	f_1 = fs[0];

	// use the SFM
	[loop]
	for (uint i = 0; i < fragCount; i++)
	{
		f_2 = (Fragment)0;
		Fragment f_merge = (Fragment)0;
		uint i_next = i + 1;
		if (i_next < fragCount)
		{
			f_2 = fs[i_next];
			f_merge = MergeFragments(f_1, f_2);
		}

		if (f_merge.color.a < SAFE_MIN_HALF)
		{
			if (finalFragCount < K_NUM - 1)
			{
				fs[finalFragCount++] = f_1; 

				color_out += f_1.color * ((half)1.f - color_out.a);
				f_1 = f_2;
			}
			else
			{
				// tail //
				if (f_2.color.a >= SAFE_MIN_HALF)
				{
					half4 f_1_vis = f_1.color;
					half4 f_2_vis = f_2.color;
					f_1_vis += f_2_vis * ((half)1.f - f_1_vis.a);
					f_1.color = f_1_vis;
					f_1.zthick = (half)(f_2.z - f_1.z + (float)f_1.zthick);
					f_1.z = f_2.z;
					f_1.opacity_sum += f_2.opacity_sum;
				}
			}
		}
		else
		{
			f_1 = f_merge;
		}
	}

	if (f_1.color.a >= SAFE_MIN_HALF)
	{
		fs[finalFragCount++] = f_1;

		color_out += f_1.color * ((half)1.f - color_out.a);
	}

	return color_out;
}