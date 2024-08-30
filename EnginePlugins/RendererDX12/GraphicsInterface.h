#pragma once
#ifdef _WIN32
#define DX12_EXPORT __declspec(dllexport)
#else
#define DX12_EXPORT __attribute__((visibility("default")))
#endif

namespace vz::graphics
{
	class GraphicsDevice;

	extern "C" DX12_EXPORT bool Initialize();
	extern "C" DX12_EXPORT GraphicsDevice* GetGraphicsDevice();
}