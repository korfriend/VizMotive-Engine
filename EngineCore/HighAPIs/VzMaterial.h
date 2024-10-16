#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzMaterial : VzResource
	{
		enum class TextureSlot : uint32_t
		{
			BASECOLORMAP = 0,
			VOLUME_DENSITYMAP, // this is used for volume rendering

			TEXTURESLOT_COUNT
		};

		VzMaterial(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, "VzMaterial", RES_COMPONENT_TYPE::MATERIAL) {}

		void SetTexture(const VID vid, const TextureSlot slot);
		void SetTexture(const VzResource* res, const TextureSlot slot) { SetTexture(res->GetVID(), slot); }
	};
}
