#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzTexture : VzResource
	{
		VzTexture(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, COMPONENT_TYPE::TEXTURE) {}

	};
}
