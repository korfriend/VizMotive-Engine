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
			VOLUME_SCULPT_MAP,

			VOLUME_TEXTURESLOT_COUNT
		};
		enum class LookupTableSlot : uint32_t
		{
			LOOKUP_COLOR,
			LOOKUP_OTF,
			LOOKUP_WINDOWING,

			LOOKUPTABLE_COUNT
		};

		enum class ShaderType : uint32_t
		{
			PHONG = 0,
			PBR,
			UNLIT,

			COUNT
		};

		VzMaterial(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, COMPONENT_TYPE::MATERIAL) {}

		void SetTexture(const VID vid, const TextureSlot slot);
		void SetTexture(const VzResource* res, const TextureSlot slot) { SetTexture(res->GetVID(), slot); }
		void SetVolumeTexture(const VID vid, const VolumeTextureSlot slot);
		void SetVolumeTexture(const VzResource* res, const VolumeTextureSlot slot) { SetVolumeTexture(res->GetVID(), slot); }
		void SetLookupTable(const VID vid, const LookupTableSlot slot);
		void SetLookupTable(const VzResource* res, const LookupTableSlot slot) { SetLookupTable(res->GetVID(), slot); }

		void SetShaderType(const ShaderType shaderType);
		void SetDoubleSided(const bool enabled);
		void SetBaseColor(const vfloat4& color);
		void SetGaussianSplattingEnabled(const bool enabled);
		void SetWireframe(const bool enabled);
		
		ShaderType GetShaderType() const;
		vfloat4 GetBaseColor() const;
		bool IsDoubleSided() const;
	};
	using TextureSlot = VzMaterial::TextureSlot;
	using VolumeTextureSlot = VzMaterial::VolumeTextureSlot;
	using LookupTableSlot = VzMaterial::LookupTableSlot;
	using ShaderType = VzMaterial::ShaderType;
}
