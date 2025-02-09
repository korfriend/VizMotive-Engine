#pragma once
#include "GBackend/GBackendDevice.h"

#ifdef _WIN32
#define VULKAN_EXPORT __declspec(dllexport)
#else
#define VULKAN_EXPORT __attribute__((visibility("default")))
#endif

// Note: this header file will not be included as an interface

namespace vz
{
	// PluginInterface.cpp
	extern "C" VULKAN_EXPORT bool Initialize(graphics::ValidationMode validationMode, graphics::GPUPreference preference);
	extern "C" VULKAN_EXPORT graphics::GraphicsDevice* GetGraphicsDevice();
	extern "C" VULKAN_EXPORT void Deinitialize();
}