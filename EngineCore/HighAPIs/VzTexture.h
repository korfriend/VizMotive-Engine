#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzTexture : VzResource
	{
		// the same as graphics::Format and TextureComponent::TextureFormat
		enum class TextureFormat : uint8_t
		{
			UNKNOWN,

			R32G32B32A32_FLOAT,
			R32G32B32A32_UINT,
			R32G32B32A32_SINT,

			R32G32B32_FLOAT,
			R32G32B32_UINT,
			R32G32B32_SINT,

			R16G16B16A16_FLOAT,
			R16G16B16A16_UNORM,
			R16G16B16A16_UINT,
			R16G16B16A16_SNORM,
			R16G16B16A16_SINT,

			R32G32_FLOAT,
			R32G32_UINT,
			R32G32_SINT,
			D32_FLOAT_S8X24_UINT,	// depth (32-bit) + stencil (8-bit) | SRV: R32_FLOAT (default or depth aspect), R8_UINT (stencil aspect)

			R10G10B10A2_UNORM,
			R10G10B10A2_UINT,
			R11G11B10_FLOAT,
			R8G8B8A8_UNORM,
			R8G8B8A8_UNORM_SRGB,
			R8G8B8A8_UINT,
			R8G8B8A8_SNORM,
			R8G8B8A8_SINT,
			B8G8R8A8_UNORM,
			B8G8R8A8_UNORM_SRGB,
			R16G16_FLOAT,
			R16G16_UNORM,
			R16G16_UINT,
			R16G16_SNORM,
			R16G16_SINT,
			D32_FLOAT,				// depth (32-bit) | SRV: R32_FLOAT
			R32_FLOAT,
			R32_UINT,
			R32_SINT,
			D24_UNORM_S8_UINT,		// depth (24-bit) + stencil (8-bit) | SRV: R24_INTERNAL (default or depth aspect), R8_UINT (stencil aspect)
			R9G9B9E5_SHAREDEXP,

			R8G8_UNORM,
			R8G8_UINT,
			R8G8_SNORM,
			R8G8_SINT,
			R16_FLOAT,
			D16_UNORM,				// depth (16-bit) | SRV: R16_UNORM
			R16_UNORM,
			R16_UINT,
			R16_SNORM,
			R16_SINT,

			R8_UNORM,
			R8_UINT,
			R8_SNORM,
			R8_SINT,

			// Formats that are not usable in render pass must be below because formats in render pass must be encodable as 6 bits:

			BC1_UNORM,			// Three color channels (5 bits:6 bits:5 bits), with 0 or 1 bit(s) of alpha
			BC1_UNORM_SRGB,		// Three color channels (5 bits:6 bits:5 bits), with 0 or 1 bit(s) of alpha
			BC2_UNORM,			// Three color channels (5 bits:6 bits:5 bits), with 4 bits of alpha
			BC2_UNORM_SRGB,		// Three color channels (5 bits:6 bits:5 bits), with 4 bits of alpha
			BC3_UNORM,			// Three color channels (5 bits:6 bits:5 bits) with 8 bits of alpha
			BC3_UNORM_SRGB,		// Three color channels (5 bits:6 bits:5 bits) with 8 bits of alpha
			BC4_UNORM,			// One color channel (8 bits)
			BC4_SNORM,			// One color channel (8 bits)
			BC5_UNORM,			// Two color channels (8 bits:8 bits)
			BC5_SNORM,			// Two color channels (8 bits:8 bits)
			BC6H_UF16,			// Three color channels (16 bits:16 bits:16 bits) in "half" floating point
			BC6H_SF16,			// Three color channels (16 bits:16 bits:16 bits) in "half" floating point
			BC7_UNORM,			// Three color channels (4 to 7 bits per channel) with 0 to 8 bits of alpha
			BC7_UNORM_SRGB,		// Three color channels (4 to 7 bits per channel) with 0 to 8 bits of alpha

			NV12,				// video YUV420; SRV Luminance aspect: R8_UNORM, SRV Chrominance aspect: R8G8_UNORM
		};

		VzTexture(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, COMPONENT_TYPE::TEXTURE) {}

		bool LoadImageFile(const std::string& fileName, const bool isLinear = true, const bool generateMIPs = true);
		bool LoadMemory(const std::string& name, const std::vector<uint8_t>& data, const TextureFormat textureFormat,
			const uint32_t w, const uint32_t h, const uint32_t d);
		//std::string GetImageFileName();
		//// sampler
		//void SetMinFilter(const SamplerMinFilter filter);
		//void SetMagFilter(const SamplerMagFilter filter);
		//void SetWrapModeS(const SamplerWrapMode mode);
		//void SetWrapModeT(const SamplerWrapMode mode);
		//
		//bool GenerateMIPs();
	};

	using TextureFormat = VzTexture::TextureFormat;

	struct API_EXPORT VzVolume : VzTexture
	{
		VzVolume(const VID vid, const std::string& originFrom)
			: VzTexture(vid, originFrom) { type_ = COMPONENT_TYPE::VOLUME; }
	};
}
