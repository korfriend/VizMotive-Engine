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
		textureComponents_[SCU32(textureSlot)] = texture->GetVUID();
	}

	void MaterialComponent::SetVolumeTexture(const Entity volumetextureEntity, const VolumeTextureSlot volumetextureSlot)
	{
		VolumeComponent* volume = compfactory::GetVolumeComponent(volumetextureEntity);
		if (volume == nullptr)
		{
			backlog::post("MaterialComponent::SetVolumeTexture >> Invalid texture entity!!", backlog::LogLevel::Warn);
			return;
		}
		volumeComponents_[SCU32(volumetextureSlot)] = volume->GetVUID();
	}
}

namespace vz
{

	void GMaterialComponent::UpdateAssociatedTextures()
	{
		for (uint32_t slot = 0; slot < SCU32(TextureSlot::TEXTURESLOT_COUNT); ++slot)
		{
			Entity texture_entity = compfactory::GetEntityByVUID(textureComponents_[slot]);
			//GTextureComponent* texture = (GTextureComponent*)compfactory::GetTextureComponent(texture_entity);
			//
			//auto& textureslot = textures[slot];
			//if (textureslot.resource.IsValid())
			//{
			//	textureslot.resource.SetOutdated();
			//}
		}

		//for (uint32_t slot = 0; slot < TEXTURESLOT_COUNT; ++slot)
		//{
		//	auto& textureslot = textures[slot];
		//	if (!textureslot.name.empty())
		//	{
		//		wi::resourcemanager::Flags flags = GetTextureSlotResourceFlags(TEXTURESLOT(slot));
		//		textureslot.resource = wi::resourcemanager::Load(textureslot.name, flags);
		//	}
		//}
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