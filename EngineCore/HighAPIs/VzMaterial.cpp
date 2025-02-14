#include "VzEngineAPIs.h"
#include "Components/GComponents.h"
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

	void VzMaterial::SetLookupTable(const VID vid, const LookupTableSlot slot)
	{
		GET_MATERIAL_COMP(material, );
		material->SetLookupTable(vid, static_cast<MaterialComponent::LookupTableSlot>(slot));
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

	void VzMaterial::SetBaseColor(const vfloat4& color)
	{
		GET_MATERIAL_COMP(material, );
		material->SetBaseColor(*(XMFLOAT4*)&color);
	}

	void VzMaterial::SetGaussianSplattingEnabled(const bool enabled)
	{
		GET_MATERIAL_COMP(material, );
		material->SetGaussianSplatting(enabled);
	}

	void VzMaterial::SetWireframe(const bool enabled)
	{
		GET_MATERIAL_COMP(material, );
		material->SetWireframe(enabled);
	}

	vfloat4 VzMaterial::GetBaseColor() const
	{
		GET_MATERIAL_COMP(material, {});
		return __FC4 material->GetBaseColor();
	}

	bool VzMaterial::IsDoubleSided() const
	{
		GET_MATERIAL_COMP(material, false);
		return material->IsDoubleSided();
	}
}