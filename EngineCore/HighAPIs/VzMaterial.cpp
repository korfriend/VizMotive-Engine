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
		UpdateTimeStamp();
	}

	void VzMaterial::SetVolumeTexture(const VID vid, const VolumeTextureSlot slot)
	{
		GET_MATERIAL_COMP(material, );
		material->SetVolumeTexture(vid, static_cast<MaterialComponent::VolumeTextureSlot>(slot));
		UpdateTimeStamp();
	}

	void VzMaterial::SetLookupTable(const VID vid, const LookupTableSlot slot)
	{
		GET_MATERIAL_COMP(material, );
		material->SetLookupTable(vid, static_cast<MaterialComponent::LookupTableSlot>(slot));
		UpdateTimeStamp();
	}

	void VzMaterial::SetShaderType(const ShaderType shaderType)
	{
		GET_MATERIAL_COMP(material, );
		material->SetShaderType(static_cast<MaterialComponent::ShaderType>(shaderType));
		UpdateTimeStamp();
	}

	ShaderType VzMaterial::GetShaderType() const
	{
		GET_MATERIAL_COMP(material, ShaderType::PHONG);
		return static_cast<ShaderType>(material->GetShaderType());
	}

	void VzMaterial::SetDoubleSided(const bool enabled)
	{
		GET_MATERIAL_COMP(material, );
		material->SetDoubleSidedEnabled(enabled);
		UpdateTimeStamp();
	}

	void VzMaterial::SetShadowCast(const bool enabled)
	{
		GET_MATERIAL_COMP(material, );
		material->SetShadowCastEnabled(enabled);
		UpdateTimeStamp();
	}

	void VzMaterial::SetShadowReceive(const bool enabled)
	{
		GET_MATERIAL_COMP(material, );
		material->SetShadowReceiveEnabled(enabled);
		UpdateTimeStamp();
	}

	void VzMaterial::SetBaseColor(const vfloat4& color)
	{
		GET_MATERIAL_COMP(material, );
		material->SetBaseColor(*(XMFLOAT4*)&color);
		UpdateTimeStamp();
	}

	void VzMaterial::SetGaussianSplattingEnabled(const bool enabled)
	{
		GET_MATERIAL_COMP(material, );
		material->SetGaussianSplattingEnabled(enabled);
		UpdateTimeStamp();
	}

	void VzMaterial::SetWireframe(const bool enabled)
	{
		GET_MATERIAL_COMP(material, );
		material->SetWireframeEnabled(enabled);
		UpdateTimeStamp();
	}

	void VzMaterial::SetPhongFactors(const vfloat4 phongFactors)
	{
		GET_MATERIAL_COMP(material, );
		material->SetPhongFactors(*(XMFLOAT4*)&phongFactors);
		UpdateTimeStamp();
	}

	void VzMaterial::SetVolumeMapper(const ActorVID targetVolumeActorVID, const VolumeTextureSlot volumetextureSlot, const LookupTableSlot lookuptextureSlot)
	{
		GET_MATERIAL_COMP(material, );
		material->SetVolumeMapper(
			targetVolumeActorVID, 
			static_cast<MaterialComponent::VolumeTextureSlot>(volumetextureSlot), 
			static_cast<MaterialComponent::LookupTableSlot>(lookuptextureSlot)
		);
		UpdateTimeStamp();
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

	bool VzMaterial::IsShadowCast() const
	{
		GET_MATERIAL_COMP(material, false);
		return material->IsShadowCast();
	}

	bool VzMaterial::IsShadowReceive() const
	{
		GET_MATERIAL_COMP(material, false);
		return material->IsShadowReceive();
	}
}