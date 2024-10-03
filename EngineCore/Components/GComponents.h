#pragma once
#include "Components.h"
#include "Common/Backend/GBackendDevice.h"

namespace vz
{
	// resources

	struct GMaterialComponent : MaterialComponent
	{
		GMaterialComponent(const Entity entity, const VUID vuid = 0) : MaterialComponent(entity, vuid) {}

		// Create texture resources for GPU
		void UpdateAssociatedTextures();
	};

	struct GGeometryComponent : GeometryComponent
	{
		GGeometryComponent(const Entity entity, const VUID vuid = 0) : GeometryComponent(entity, vuid) {}
	};

	struct GTextureComponent : TextureComponent
	{
	private:
	public:
		GTextureComponent(const Entity entity, const VUID vuid = 0);
		int GetTextureSRGBSubresource() const;
		const graphics::Texture& GetTexture() const;
		// Allows to set a Texture to the resource from outside
		//	srgb_subresource: you can provide a subresource for SRGB view if the texture is going to be used as SRGB with the GetTextureSRGBSubresource() (optional)
		void SetTexture(const graphics::Texture& texture, int srgb_subresource = -1);
		// Let the streaming system know the required resolution of this resource
		void StreamingRequestResolution(uint32_t resolution);
		const graphics::GPUResource* GetGPUResource() const
		{
			if (!IsValid() || !GetTexture().IsValid())
				return nullptr;
			return &GetTexture();
		}
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
