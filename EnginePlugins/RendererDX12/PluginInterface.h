#pragma once
#include "Common/Backend/GBackendDevice.h"
#include "Common/Backend/GSceneRenderPath.h"

#ifdef _WIN32
#define DX12_EXPORT __declspec(dllexport)
#else
#define DX12_EXPORT __attribute__((visibility("default")))
#endif
#include <string>
#include <vector>

// Note: this header file will not be included as an interface

namespace vz
{
	// PluginInterface.cpp
	extern "C" DX12_EXPORT bool Initialize(graphics::ValidationMode validationMode, graphics::GPUPreference preference);
	extern "C" DX12_EXPORT graphics::GraphicsDevice* GetGraphicsDevice();
	extern "C" DX12_EXPORT void Deinitialize();

	// Renderer.cpp
	extern "C" DX12_EXPORT GScene* NewGScene(Scene* scene);
	extern "C" DX12_EXPORT GRenderPath3D* NewGRenderPath(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal);
	extern "C" DX12_EXPORT void AddDeferredMIPGen(const graphics::Texture& texture, bool preserve_coverage);
	extern "C" DX12_EXPORT void AddDeferredBlockCompression(const graphics::Texture& texture_src, const graphics::Texture& texture_bc);

	// RenderInitializer.cpp
	extern "C" DX12_EXPORT bool InitRenderer();

	// ShaderLoader.cpp
	extern "C" DX12_EXPORT bool LoadShader(
		graphics::ShaderStage stage,
		graphics::Shader& shader,
		const std::string& filename,
		graphics::ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines);
	extern "C" DX12_EXPORT bool LoadShaders();

}