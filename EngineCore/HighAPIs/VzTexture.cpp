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
	//std::string GetImageFileName();
	//void SetMinFilter(const SamplerMinFilter filter);
	//void SetMagFilter(const SamplerMagFilter filter);
	//void SetWrapModeS(const SamplerWrapMode mode);
	//void SetWrapModeT(const SamplerWrapMode mode);
	//
	//bool GenerateMIPs();
}