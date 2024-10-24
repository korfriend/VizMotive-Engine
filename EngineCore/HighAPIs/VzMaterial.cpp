#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_MATERIAL_COMP(COMP, RET) MaterialComponent* COMP = compfactory::GetMaterialComponent(componentVID_); \
	if (!COMP) {post("MaterialComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzMaterial::SetTexture(const VID vid, const TextureSlot slot)
	{
		GET_MATERIAL_COMP(material, );
		material->SetTexture(vid, static_cast<MaterialComponent::TextureSlot>(slot));
	}
}