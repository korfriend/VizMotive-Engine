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

	void VzMaterial::SetVolumeTexture(const VID vid, const VolumeTextureSlot slot)
	{
		GET_MATERIAL_COMP(material, );
		material->SetVolumeTexture(vid, static_cast<MaterialComponent::VolumeTextureSlot>(slot));
	}

	void VzMaterial::SetShaderType(const ShaderType shaderType)
	{
		GET_MATERIAL_COMP(material, );
		material->SetShaderType(static_cast<MaterialComponent::ShaderType>(shaderType));
	}

	ShaderType VzMaterial::GetShaderType() const
	{
		GET_MATERIAL_COMP(material, ShaderType::PHONG);
		return static_cast<ShaderType>(material->GetShaderType());
	}

	void VzMaterial::SetDoubleSided(const bool enabled)
	{
		GET_MATERIAL_COMP(material, );
		material->SetDoubleSided(enabled);
	}

	bool VzMaterial::IsDoubleSided() const
	{
		GET_MATERIAL_COMP(material, false);
		return material->IsDoubleSided();
	}
}