#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzMaterial : VzResource
	{
		enum class TextureSlot : uint32_t
		{
			BASECOLORMAP = 0,
			NORMALMAP,
			SURFACEMAP,
			EMISSIVEMAP,
			DISPLACEMENTMAP,
			OCCLUSIONMAP,
			TRANSMISSIONMAP,
			SHEENCOLORMAP,
			SHEENROUGHNESSMAP,
			CLEARCOATMAP,
			CLEARCOATROUGHNESSMAP,
			CLEARCOATNORMALMAP,
			SPECULARMAP,
			ANISOTROPYMAP,
			TRANSPARENCYMAP,

			TEXTURESLOT_COUNT
		};
		enum class VolumeTextureSlot : uint32_t
		{
			VOLUME_DENSITYMAP, // this is used for volume rendering
			VOLUME_SEMANTICMAP,
			VOLUME_SCULPTMAP,

			VOLUME_TEXTURESLOT_COUNT
		};

		enum class ShaderType : uint32_t
		{
			PHONG = 0,
			PBR,
			UNLIT,
			VOLUMEMAP,

			COUNT
		};

		VzMaterial(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, COMPONENT_TYPE::MATERIAL) {
		}
		virtual ~VzMaterial() = default;

		void SetTexture(const VID vid, const TextureSlot slot);
		void SetTexture(const VzResource* res, const TextureSlot slot) { SetTexture(res->GetVID(), slot); }
		void SetVolumeTexture(const VID vid, const VolumeTextureSlot slot);
		void SetVolumeTexture(const VzResource* res, const VolumeTextureSlot slot) { SetVolumeTexture(res->GetVID(), slot); }
		void SetLookupTable(const VID vid, const LookupTableSlot slot);
		void SetLookupTable(const VzResource* res, const LookupTableSlot slot) { SetLookupTable(res->GetVID(), slot); }

		void SetShaderType(const ShaderType shaderType);
		
		void SetDoubleSided(const bool enabled);
		void SetShadowCast(const bool enabled);
		void SetShadowReceive(const bool enabled);
		void SetBaseColor(const vfloat4& color);
		void SetGaussianSplattingEnabled(const bool enabled);
		void SetWireframe(const bool enabled);
		void SetPhongFactors(const vfloat4 phongFactors);

		void SetVolumeMapper(const ActorVID targetVolumeActorVID, const VolumeTextureSlot volumetextureSlot, const LookupTableSlot lookuptextureSlot);

		ShaderType GetShaderType() const;
		vfloat4 GetBaseColor() const;
		bool IsDoubleSided() const;
		bool IsShadowCast() const;
		bool IsShadowReceive() const;
	};
	using TextureSlot = VzMaterial::TextureSlot;
	using VolumeTextureSlot = VzMaterial::VolumeTextureSlot;
	using ShaderType = VzMaterial::ShaderType;
}
