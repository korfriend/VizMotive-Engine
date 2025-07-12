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
		vuidTextureComponents_[SCU32(textureSlot)] = texture->GetVUID();
		timeStampSetter_ = TimerNow;
	}

	void MaterialComponent::SetVolumeTexture(const Entity volumetextureEntity, const VolumeTextureSlot volumetextureSlot)
	{
		VolumeComponent* volume = compfactory::GetVolumeComponent(volumetextureEntity);
		if (volume == nullptr)
		{
			backlog::post("MaterialComponent::SetVolumeTexture >> Invalid texture entity!!", backlog::LogLevel::Warn);
			return;
		}
		vuidVolumeTextureComponents_[SCU32(volumetextureSlot)] = volume->GetVUID();
		timeStampSetter_ = TimerNow;
	}

	void MaterialComponent::SetLookupTable(const Entity lookuptextureEntity, const LookupTableSlot lookuptextureSlot)
	{
		TextureComponent* texture = compfactory::GetTextureComponent(lookuptextureEntity);
		if (texture == nullptr)
		{
			backlog::post("MaterialComponent::SetLookupTable >> Invalid texture entity!!", backlog::LogLevel::Warn);
			return;
		}
		vuidLookupTextureComponents_[SCU32(lookuptextureSlot)] = texture->GetVUID();
		timeStampSetter_ = TimerNow;
	}

	void MaterialComponent::SetVolumeMapper(const Entity targetRenderableEntity, const VolumeTextureSlot volumetextureSlot, const LookupTableSlot lookupSlot)
	{
		RenderableComponent* renderable = compfactory::GetRenderableComponent(targetRenderableEntity);
		timeStampSetter_ = TimerNow;
		volumemapperVolumeSlot_ = volumetextureSlot;
		volumemapperLookupSlot_ = lookupSlot;
		if (renderable)
		{
			vuidVolumeMapperRenderable_ = renderable->GetVUID();
		}
		else
		{
			vuidVolumeMapperRenderable_ = INVALID_VUID;
		}
	}

}

namespace vz
{
	void GMaterialComponent::UpdateAssociatedTextures()
	{
		for (uint32_t slot = 0; slot < SCU32(TextureSlot::TEXTURESLOT_COUNT); ++slot)
		{
			Entity texture_entity = compfactory::GetEntityByVUID(vuidTextureComponents_[slot]);
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
		uint32_t filter_mask_flags = 0;
		if (baseColor_.w < 0.99f)
		{
			filter_mask_flags |= FILTER_TRANSPARENT;
		}
		if (blendMode_ == BlendMode::BLENDMODE_OPAQUE)
		{
			filter_mask_flags |= FILTER_OPAQUE;
		}
		if (IsGaussianSplattingEnabled())
		{
			filter_mask_flags |= FILTER_GAUSSIAN_SPLATTING;
		}
		VolumeComponent* volume = compfactory::GetVolumeComponentByVUID(vuidVolumeTextureComponents_[SCU32(VolumeTextureSlot::VOLUME_MAIN_MAP)]);
		if (volume)
		{
			filter_mask_flags |= FILTER_VOLUME;
		}

		return filter_mask_flags;
	}
}	