#include "GComponents.h"

namespace vz
{
	void GMaterialComponent::UpdateAssociatedTextures()
	{
		// to do //
	}

	uint32_t GMaterialComponent::GetFilterMaskFlags() const
	{
		if (baseColor_.w < 0.99f)
		{
			return FILTER_TRANSPARENT;
		}
		if (blendMode_ == BlendMode::BLENDMODE_OPAQUE)
		{
			return FILTER_OPAQUE;
		}
		return FILTER_TRANSPARENT;
	}
}	