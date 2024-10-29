#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzMaterial : VzResource
	{
		enum class TextureSlot : uint32_t
		{
			BASECOLORMAP = 0,
			VOLUME_DENSITYMAP, // this is used for volume rendering

			TEXTURESLOT_COUNT
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

		void SetShaderType(const ShaderType shaderType);
		ShaderType GetShaderType() const;
		void SetDoubleSided(const bool enabled);
		bool IsDoubleSided() const;
	};
	using TextureSlot = VzMaterial::TextureSlot;
	using ShaderType = VzMaterial::ShaderType;
}
