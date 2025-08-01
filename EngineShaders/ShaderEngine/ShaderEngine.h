#pragma once
#include "GBackend/GBackendDevice.h"
#include "GBackend/GShaderInterface.h"

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
	using Entity = uint64_t;

	// ShaderEngine.cpp
	extern "C" DX12_EXPORT bool Initialize(graphics::GraphicsDevice* device);
	extern "C" DX12_EXPORT bool LoadRenderer();
	extern "C" DX12_EXPORT bool ApplyConfiguration();
	extern "C" DX12_EXPORT void Deinitialize();

	// Renderer.cpp
	extern "C" DX12_EXPORT GScene* NewGScene(Scene* scene);
	extern "C" DX12_EXPORT GRenderPath3D* NewGRenderPath(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal);

	extern "C" DX12_EXPORT void AddDeferredMIPGen(const graphics::Texture& texture, bool preserve_coverage);
	extern "C" DX12_EXPORT void AddDeferredBlockCompression(const graphics::Texture& texture_src, const graphics::Texture& texture_bc);
	extern "C" DX12_EXPORT void AddDeferredTextureCopy(const graphics::Texture& texture_src, const graphics::Texture& texture_dst, const bool mipGen);
	extern "C" DX12_EXPORT void AddDeferredBufferUpdate(const graphics::GPUBuffer& buffer, const void* data, const uint64_t size = ~0, const uint64_t offset = 0);
	extern "C" DX12_EXPORT void AddDeferredGeometryGPUBVHUpdate(const Entity entity);

	// ShaderLoader.cpp
	extern "C" DX12_EXPORT bool LoadShader(
		graphics::ShaderStage stage,
		graphics::Shader& shader,
		const std::string& filename,
		graphics::ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines
	);
	extern "C" DX12_EXPORT bool LoadShaders();

}