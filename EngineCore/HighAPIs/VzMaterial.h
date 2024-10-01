#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzMaterial : VzResource
	{
		VzMaterial(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, "VzMaterial", RES_COMPONENT_TYPE::MATERIAL) {}

	};
}
