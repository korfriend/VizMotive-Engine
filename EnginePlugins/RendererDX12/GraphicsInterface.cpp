#include "GraphicsInterface.h"
#include "GraphicsDevice_DX12.h"

#include <memory>

namespace vz::graphics
{
	std::unique_ptr<GraphicsDevice> graphicsDevice;

	bool Initialize()
	{
		ValidationMode validationMode = ValidationMode::Disabled;
		GPUPreference preference = GPUPreference::Discrete;
		//wi::renderer::SetShaderPath(wi::renderer::GetShaderPath() + "hlsl6/");
		graphicsDevice = std::make_unique<GraphicsDevice_DX12>(validationMode, preference);
		return graphicsDevice.get() != nullptr;
	}

	bool Deinitialize()
	{
		graphicsDevice.reset();
	}

	GraphicsDevice* GetGraphicsDevice()
	{
		return (GraphicsDevice*)graphicsDevice.get();
	}
}