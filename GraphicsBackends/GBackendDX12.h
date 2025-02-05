#pragma once
#include "Common/Backend/GBackendDevice.h"

#ifdef _WIN32
#define DX12_EXPORT __declspec(dllexport)
#else
#define DX12_EXPORT __attribute__((visibility("default")))
#endif

// Note: this header file will not be included as an interface

namespace vz
{
	extern "C" DX12_EXPORT bool Initialize(graphics::ValidationMode validationMode, graphics::GPUPreference preference);
	extern "C" DX12_EXPORT graphics::GraphicsDevice* GetGraphicsDevice();
	extern "C" DX12_EXPORT void Deinitialize();
}