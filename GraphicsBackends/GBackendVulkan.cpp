#include "GBackendVulkan.h"
//#include "GraphicsDevice_Vulkan.h"

#include <memory>

namespace vz
{
	using namespace graphics;

	std::unique_ptr<GraphicsDevice> graphicsDevice;

	bool Initialize(ValidationMode validationMode, GPUPreference preference)
	{
		//graphicsDevice = std::make_unique<GraphicsDevice_DX12>(validationMode, preference);
		//GetDevice() = graphicsDevice.get();
		//return graphicsDevice.get() != nullptr;
		return true;
	}

	void Deinitialize()
	{
		graphicsDevice.reset();
	}

	GraphicsDevice* GetGraphicsDevice()
	{
		return (GraphicsDevice*)graphicsDevice.get();
	}
}