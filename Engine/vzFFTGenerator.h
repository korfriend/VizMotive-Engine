#pragma once
#include "CommonInclude.h"
#include "vzGraphicsDevice.h"

namespace vz::fftgenerator
{

	struct CSFFT512x512_Plan
	{
		// More than one array can be transformed at same time
		uint32_t slices;

		// For 512x512 config, we need 6 constant buffers
		vz::graphics::GPUBuffer pRadix008A_CB[6];

		vz::graphics::GPUBuffer pBuffer_Tmp;
	};


	void fft512x512_create_plan(CSFFT512x512_Plan& plan, uint32_t slices);

	void fft_512x512_c2c(
		const CSFFT512x512_Plan& fft_plan,
		const vz::graphics::GPUResource& pUAV_Dst,
		const vz::graphics::GPUResource& pSRV_Dst,
		const vz::graphics::GPUResource& pSRV_Src,
		vz::graphics::CommandList cmd);

	void LoadShaders();
}
