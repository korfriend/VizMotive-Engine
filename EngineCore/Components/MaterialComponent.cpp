#include "GComponents.h"
#include "Utils/Backlog.h"

namespace vz
{
	void MaterialComponent::SetTexture(const Entity textureEntity, const TextureSlot textureSlot)
	{
		TextureComponent* texture = compfactory::GetTextureComponent(textureEntity);
		if (texture == nullptr)
		{
			backlog::post("MaterialComponent::SetTexture >> Invalid texture entity!!", backlog::LogLevel::Warn);
			return;
		}
		if (textureSlot == TextureSlot::VOLUME_DENSITYMAP)
		{
			if (texture->GetComponentType() != ComponentType::VOLUMETEXTURE)
			{
				backlog::post("MaterialComponent::SetTexture >> TextureSlot::VOLUME_DENSITYMAP requires VolumeTexture! (here, just a Texture)", backlog::LogLevel::Warn);
			}
		}

		textureComponents_[SCU32(textureSlot)] = texture->GetVUID();
	}
}

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