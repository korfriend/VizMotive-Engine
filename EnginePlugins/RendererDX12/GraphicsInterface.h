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
	extern "C" DX12_EXPORT bool Deinitialize();
	//extern "C" DX12_EXPORT bool Render1(Scene, Camera, Canvas);
	//extern "C" DX12_EXPORT bool Render2(Scene, Camera, Canvas);
	//extern "C" DX12_EXPORT bool Render3(Scene, Camera, Canvas);
	//...
}