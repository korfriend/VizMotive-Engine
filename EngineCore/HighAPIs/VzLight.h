#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzLight : VzSceneComp
	{
		VzLight(const VID vid, const std::string& originFrom)
			: VzSceneComp(vid, originFrom, "VzLight", SCENE_COMPONENT_TYPE::LIGHT) {}
	};
}