#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzGeometry : VzResource
	{
		VzGeometry(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, "VzGeometry", RES_COMPONENT_TYPE::GEOMATRY) {}

		void MaskTestTriangle();
	};
}
