#include "PluginInterface.h"
#include "GraphicsDevice_DX12.h"

#include "Renderer.h"

#include <memory>

namespace vz
{
	using namespace graphics;

	std::unique_ptr<GraphicsDevice> graphicsDevice;

	bool Initialize(ValidationMode validationMode, GPUPreference preference)
	{
		std::string version = vz::GetComponentVersion();
		assert(version == vz::COMPONENT_INTERFACE_VERSION);

		//renderer::SetShaderPath(renderer::GetShaderPath() + "hlsl6/");
		graphicsDevice = std::make_unique<GraphicsDevice_DX12>(validationMode, preference);
		GetDevice() = graphicsDevice.get();
		return graphicsDevice.get() != nullptr;
	}

	void Deinitialize()
	{
		initializer::ReleaseResources();
		graphicsDevice.reset();
	}

	GraphicsDevice* GetGraphicsDevice()
	{
		return (GraphicsDevice*)graphicsDevice.get();
	}
}