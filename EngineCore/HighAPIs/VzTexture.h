#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzTexture : VzResource
	{
		VzTexture(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, COMPONENT_TYPE::TEXTURE) {}

	};

	struct API_EXPORT VzVolume : VzTexture
	{
		VzVolume(const VID vid, const std::string& originFrom)
			: VzTexture(vid, originFrom) { type_ = COMPONENT_TYPE::VOLUME; }

	};
}
