#include "ShaderEngine.h"
#include "Common/Backend/GBackendDevice.h"

#include "Renderer.h"

#include <memory>

namespace vz
{
	using namespace graphics;

	bool Initialize(graphics::GraphicsDevice* device)
	{
		std::string version = vz::GetComponentVersion();
		assert(version == vz::COMPONENT_INTERFACE_VERSION);

		//renderer::SetShaderPath(renderer::GetShaderPath() + "hlsl6/");
		graphics::GetDevice() = device;

		return true;
	}

	void Deinitialize()
	{
		initializer::ReleaseResources();
	}
}