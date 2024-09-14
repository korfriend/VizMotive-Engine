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

		// to do //
	};

	struct GGeometryComponent : GeometryComponent
	{
	};

	struct GTextureComponent : TextureComponent
	{

	};

	// scene 

	struct GRenderableComponent : RenderableComponent
	{
		// internal geometry
		// internal mi
		// internal texture
	};
}
