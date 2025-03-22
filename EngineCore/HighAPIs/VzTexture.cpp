#include "VzEngineAPIs.h"
#include "Components/GComponents.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_TEXTURE_COMP(COMP, RET) TextureComponent* COMP = compfactory::GetTextureComponent(componentVID_); \
	if (!COMP) {post("TextureComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	bool VzTexture::CreateTextureFromImageFile(const std::string& fileName)
	{
		GET_TEXTURE_COMP(texture, false);
		UpdateTimeStamp();
		return texture->LoadImageFile(fileName);
	}

	bool VzTexture::CreateTextureFromMemory(const std::string& name, const std::vector<uint8_t>& data, const TextureFormat textureFormat, const uint32_t w, const uint32_t h, const uint32_t d)
	{
		GET_TEXTURE_COMP(texture, false);
		UpdateTimeStamp();
		return texture->LoadMemory(name, data, static_cast<TextureComponent::TextureFormat>(textureFormat), w, h, d);
	}

	bool VzTexture::CreateLookupTexture(const std::string& name, const std::vector<uint8_t>& data, const TextureFormat textureFormat, const uint32_t w, const uint32_t h, const uint32_t d)
	{
		GET_TEXTURE_COMP(texture, false);
		UpdateTimeStamp();
		return texture->LoadMemory(name, data, static_cast<TextureComponent::TextureFormat>(textureFormat), w, h, d);
	}

	bool VzTexture::IsValid() const
	{
		GET_TEXTURE_COMP(texture, false);
		return texture->IsValid();
	}

	bool VzTexture::UpdateLookup(const std::vector<uint8_t>& data, const float tableValidBeginX, const float tableValidEndX)
	{
		GET_TEXTURE_COMP(texture, false);
		UpdateTimeStamp();
		texture->SetTableValidBeginEndX(XMFLOAT2((float)tableValidBeginX, (float)tableValidEndX));
		texture->UpdateMemory(data);
		return true;
	}

	void VzTexture::GetTextureSize(uint32_t* w, uint32_t* h, uint32_t* d) const
	{
		GET_TEXTURE_COMP(texture, );
		if (w) *w = texture->GetWidth();
		if (h) *h = texture->GetHeight();
		if (d) *d = texture->GetDepth();
	}
	
	TextureFormat VzTexture::GetTextureFormat() const
	{
		GET_TEXTURE_COMP(texture, TextureFormat::UNKNOWN);
		return static_cast<TextureFormat>(texture->GetFormat());
	}

	std::string VzTexture::GetResourceName() const
	{
		GET_TEXTURE_COMP(texture, "");
		return texture->GetResourceName();
	}
	void VzTexture::SetSamplerFilter(const SamplerType filter)
	{
		GET_TEXTURE_COMP(texture, );
		UpdateTimeStamp();
	}
}