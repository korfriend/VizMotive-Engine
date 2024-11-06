#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_TEXTURE_COMP(COMP, RET) TextureComponent* COMP = compfactory::GetTextureComponent(componentVID_); \
	if (!COMP) {post("TextureComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	bool VzTexture::LoadImageFile(const std::string& fileName, const bool isLinear, const bool generateMIPs)
	{
		GET_TEXTURE_COMP(texture, false);
		return texture->LoadImageFile(fileName);
	}

	bool VzTexture::LoadMemory(const std::string& name, const std::vector<uint8_t>& data, const TextureFormat textureFormat,
		const uint32_t w, const uint32_t h, const uint32_t d)
	{
		GET_TEXTURE_COMP(texture, false);
		return texture->LoadMemory(name, data, static_cast<TextureComponent::TextureFormat>(textureFormat), w, h, d);
	}
	//std::string GetImageFileName();
	//void SetMinFilter(const SamplerMinFilter filter);
	//void SetMagFilter(const SamplerMagFilter filter);
	//void SetWrapModeS(const SamplerWrapMode mode);
	//void SetWrapModeT(const SamplerWrapMode mode);
	//
	//bool GenerateMIPs();
}