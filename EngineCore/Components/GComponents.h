#pragma once
#include "Components.h"
#include "Common/Backend/GBackendDevice.h"

namespace vz::resource
{

	// This can hold an asset
	//	It can be loaded from file or memory using vz::resourcemanager::Load()
	struct CORE_EXPORT GResource : Resource
	{
		const vz::graphics::Texture& GetTexture() const;
		// Allows to set a Texture to the resource from outside
		//	srgb_subresource: you can provide a subresource for SRGB view if the texture is going to be used as SRGB with the GetTextureSRGBSubresource() (optional)

		void SetTexture(const vz::graphics::Texture& texture, int srgb_subresource = -1);
	};
}

namespace vz
{
	// ECS components

	// resources

	struct GMaterialComponent : MaterialComponent
	{
		GMaterialComponent(const Entity entity, const VUID vuid = 0) : MaterialComponent(entity, vuid) {}
		// to do //
	};

	struct GGeometryComponent : GeometryComponent
	{
		GGeometryComponent(const Entity entity, const VUID vuid = 0) : GeometryComponent(entity, vuid) {}
	};

	struct GTextureComponent : TextureComponent
	{
		GTextureComponent(const Entity entity, const VUID vuid = 0) : TextureComponent(entity, vuid) {}

		inline static const std::vector<std::string> shaderTypeDefines[] = {
			{}, // SHADERTYPE_PHONG,
			{"PBR"}, // SHADERTYPE_PBR_PLANARREFLECTION,
			{"PARALLAXOCCLUSIONMAPPING"}, // SHADERTYPE_PBR_PARALLAXOCCLUSIONMAPPING,
			{"ANISOTROPIC"}, // SHADERTYPE_PBR_ANISOTROPIC,
			{"WATER"}, // SHADERTYPE_WATER,
			{"CARTOON"}, // SHADERTYPE_CARTOON,
			{"UNLIT"}, // SHADERTYPE_UNLIT,
			{"SHEEN"}, // SHADERTYPE_PBR_CLOTH,
			{"CLEARCOAT"}, // SHADERTYPE_PBR_CLEARCOAT,
			{"SHEEN", "CLEARCOAT"}, // SHADERTYPE_PBR_CLOTH_CLEARCOAT,
			{"TERRAINBLENDED"}, //SHADERTYPE_PBR_TERRAINBLENDED
		};
	};

	// scene 

	struct GRenderableComponent : RenderableComponent
	{
		GRenderableComponent(const Entity entity, const VUID vuid = 0) : RenderableComponent(entity, vuid) {}
		// internal geometry
		// internal mi
		// internal texture
	};
}
