#include "Components.h"
#include "Utils/Backlog.h"

namespace vz
{
	void HierarchyComponent::ResetRefComponents(const VUID vuidRef)
	{
		if (vuidParentHierarchy_ == vuidRef)
			vuidParentHierarchy_ = INVALID_VUID;
		auto it = children_.find(vuidRef);
		if (it != children_.end())
			children_.erase(it);
	}

	void MaterialComponent::ResetRefComponents(const VUID vuidRef)
	{
		bool is_modified = false;
		for (size_t i = 0; i < SCU32(TextureSlot::TEXTURESLOT_COUNT); i++)
		{
			if (vuidTextureComponents_[i] == vuidRef)
			{
				vuidTextureComponents_[i] = INVALID_VUID;
				is_modified = true;
			}
		}
		for (size_t i = 0; i < SCU32(VolumeTextureSlot::VOLUME_TEXTURESLOT_COUNT); i++)
		{
			if (vuidVolumeTextureComponents_[i] == vuidRef)
			{
				vuidVolumeTextureComponents_[i] = INVALID_VUID;
				is_modified = true;
			}
		}
		for (size_t i = 0; i < SCU32(LookupTableSlot::LOOKUPTABLE_COUNT); i++)
		{
			if (vuidLookupTextureComponents_[i] == vuidRef)
			{
				vuidLookupTextureComponents_[i] = INVALID_VUID;
				is_modified = true;
			}
		}
		if (vuidVolumeMapperRenderable_ == vuidRef)
		{
			vuidVolumeMapperRenderable_ = INVALID_VUID;
			is_modified = true;
		}
		if (is_modified)
		{
			UpdateAssociatedTextures();
		}
	}

	void RenderableComponent::ResetRefComponents(const VUID vuidRef)
	{
		bool is_modified = false;
		if (vuidGeometry_ == vuidRef)
		{
			vuidGeometry_ = INVALID_VUID;
			is_modified = true;
		}
		for (size_t i = 0; i < vuidMaterials_.size(); i++)
		{
			if (vuidMaterials_[i] == vuidRef)
			{
				vuidMaterials_[i] = INVALID_VUID;
				is_modified = true;
			}
		}
		if (is_modified)
		{
			updateRenderableFlags();
		}
	}
}