#include "GComponents.h"
#include "Common/ResourceManager.h"

namespace vz
{
	bool TextureComponent::IsValid() const
	{
		Resource& resource = *internalResource_.get();
		return resource.IsValid();
	}
	const std::vector<uint8_t>& TextureComponent::GetData() const
	{
		Resource& resource = *internalResource_.get();
		return resource.GetFileData();
	}
	int TextureComponent::GetFontStyle() const
	{
		Resource& resource = *internalResource_.get();
		return resource.GetFontStyle();
	}
	void TextureComponent::CopyFromData(const std::vector<uint8_t>& data)
	{
		Resource& resource = *internalResource_.get();
		resource.CopyFromData(data);
	}
	void TextureComponent::MoveFromData(std::vector<uint8_t>&& data)
	{
		Resource& resource = *internalResource_.get();
		resource.MoveFromData(std::move(data));
	}
	void TextureComponent::SetOutdated()
	{
		Resource& resource = *internalResource_.get();
		resource.SetOutdated();
	}
}

namespace vz
{
	GTextureComponent::GTextureComponent(const Entity entity, const VUID vuid) : TextureComponent(entity, vuid)
	{
		internalResource_ = std::make_shared<Resource>();
	}
	uint32_t GTextureComponent::GetUVSet() const
	{
		Resource& resource = *internalResource_.get();
		return resource.uvset;
	}
	float GTextureComponent::GetLodClamp() const
	{
		Resource& resource = *internalResource_.get();
		return resource.lod_clamp;
	}
	int GTextureComponent::GetSparseResidencymapDescriptor() const
	{
		Resource& resource = *internalResource_.get();
		return resource.sparse_residencymap_descriptor;
	}
	int GTextureComponent::GetSparseFeedbackmapDescriptor() const
	{
		Resource& resource = *internalResource_.get();
		return resource.sparse_feedbackmap_descriptor;
	}

	int GTextureComponent::GetTextureSRGBSubresource() const
	{
		Resource& resource = *internalResource_.get();
		return resource.GetTextureSRGBSubresource();
	}

	const graphics::Texture& GTextureComponent::GetTexture() const
	{
		Resource& resource = *internalResource_.get();
		return resource.GetTexture();
	}
	void GTextureComponent::SetTexture(const graphics::Texture& texture, int srgb_subresource)
	{
		Resource& resource = *internalResource_.get();
		resource.SetTexture(texture, srgb_subresource);
	}
	void GTextureComponent::StreamingRequestResolution(uint32_t resolution)
	{
		Resource& resource = *internalResource_.get();
		resource.StreamingRequestResolution(resolution);
	}
}