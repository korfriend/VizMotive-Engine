#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzTexture : VzResource
	{
		VzTexture(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, COMPONENT_TYPE::TEXTURE) {}

		bool LoadImageFile(const std::string& fileName, const bool isLinear = true, const bool generateMIPs = true);
		//std::string GetImageFileName();
		//// sampler
		//void SetMinFilter(const SamplerMinFilter filter);
		//void SetMagFilter(const SamplerMagFilter filter);
		//void SetWrapModeS(const SamplerWrapMode mode);
		//void SetWrapModeT(const SamplerWrapMode mode);
		//
		//bool GenerateMIPs();
	};

	struct API_EXPORT VzVolume : VzTexture
	{
		VzVolume(const VID vid, const std::string& originFrom)
			: VzTexture(vid, originFrom) { type_ = COMPONENT_TYPE::VOLUME; }

	};
}
