#include "GComponents.h"
#include "Utils/Backlog.h"

namespace vz
{
	bool HierarchyComponent::ResetRefComponents(const VUID vuidRef)
	{
		bool is_modified = false;
		if (vuidParentHierarchy_ == vuidRef)
		{
			vuidParentHierarchy_ = INVALID_VUID;
			is_modified = true;
		}
		auto it = children_.find(vuidRef);
		if (it != children_.end())
		{
			children_.erase(it);
			is_modified = true;
		}
		timeStampSetter_ = TimerNow;
		return is_modified;
	}

	bool MaterialComponent::ResetRefComponents(const VUID vuidRef)
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
		timeStampSetter_ = TimerNow;
		return is_modified;
	}

	bool RenderableComponent::ResetRefComponents(const VUID vuidRef)
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
		timeStampSetter_ = TimerNow;
		return is_modified;
	}
}

namespace vz
{
	bool GTextureComponent::ResetResources(const std::string& resName)
	{
		if (resName != resName_)
		{
			return false;
		}
		*this = GTextureComponent(entity_, vuid_);
		timeStampSetter_ = TimerNow;
		return true;
	}

	bool GVolumeComponent::ResetResources(const std::string& resName)
	{
		if (resName != resName_)
		{
			return false;
		}
		*this = GVolumeComponent(entity_, vuid_);
		timeStampSetter_ = TimerNow;
		return true;
	}

	bool GProbeComponent::ResetResources(const std::string& resName)
	{
		if (resName != textureName_)
		{
			return false;
		}
		resource = Resource();
		timeStampSetter_ = TimerNow;
		return true;
	}

	bool GEnvironmentComponent::ResetResources(const std::string& resName)
	{
		bool is_modified = false;
		if (resName == skyMapName_)
		{
			skyMapName_ = "";
			skyMap = Resource();
			is_modified = true;
		}
		if (resName == colorGradingMapName_)
		{
			skyMapName_ = "";
			skyMap = Resource();
			is_modified = true;
		}
		if (resName == volumetricCloudsWeatherMapFirstName_)
		{
			skyMapName_ = "";
			skyMap = Resource();
			is_modified = true;
		}
		if (resName == volumetricCloudsWeatherMapSecondName_)
		{
			skyMapName_ = "";
			skyMap = Resource();
			is_modified = true;
		}
		if (is_modified)
		{
			timeStampSetter_ = TimerNow;
			return true;
		}
		return false;
	}
}