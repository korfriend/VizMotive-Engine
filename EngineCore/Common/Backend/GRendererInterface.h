#pragma once
#include "GSceneRenderPath.h"

#include <memory>
#include <limits>

namespace vz
{
	struct GraphicsPackage
	{
		typedef bool(*PI_GraphicsInitializer)(graphics::ValidationMode validationMode, graphics::GPUPreference preference);
		typedef vz::graphics::GraphicsDevice* (*PI_GetGraphicsDevice)();
		typedef bool(*PI_GraphicsDeinitializer)();
		typedef GRenderPath3D* (*PI_NewGRenderPath3D)(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal);
		typedef GScene* (*PI_NewGScene)(Scene* scene);
		typedef bool(*PI_InitRenderer)();
		typedef void(*PI_AddDeferredMIPGen)(const graphics::Texture& texture, bool preserve_coverage);
		typedef void(*PI_AddDeferredBlockCompression)(const graphics::Texture& texture_src, const graphics::Texture& texture_bc);
		typedef void(*PI_AddDeferredTextureCopy)(const graphics::Texture& texture_src, const graphics::Texture& texture_dst, const bool mipGen);
		typedef void(*PI_AddDeferredBufferUpdate)(const graphics::GPUBuffer& buffer, const void* data, const uint64_t size, const uint64_t offset);

		typedef bool(*PI_LoadShader)(
			graphics::ShaderStage stage,
			graphics::Shader& shader,
			const std::string& filename,
			graphics::ShaderModel minshadermodel,
			const std::vector<std::string>& permutation_defines);
		typedef bool(*PI_LoadShaders)();

		// essential
		PI_GraphicsInitializer pluginInitializer = nullptr;
		PI_GraphicsDeinitializer pluginDeinitializer = nullptr;
		PI_GetGraphicsDevice pluginGetDev = nullptr;
		PI_NewGRenderPath3D pluginNewGRenderPath3D = nullptr;
		PI_NewGScene pluginNewGScene = nullptr;
		PI_InitRenderer pluginInitRenderer = nullptr;
		PI_LoadShader pluginLoadShader = nullptr;
		PI_LoadShaders pluginLoadShaders = nullptr;

		PI_AddDeferredTextureCopy pluginAddDeferredTextureCopy = nullptr;
		PI_AddDeferredBufferUpdate pluginAddDeferredBufferUpdate = nullptr;

		// optional 
		PI_AddDeferredMIPGen pluginAddDeferredMIPGen = nullptr;
		PI_AddDeferredBlockCompression pluginAddDeferredBlockCompression = nullptr;

		std::string API = "";
		std::string moduleName = "";

		bool Init(const std::string& api)
		{
			API = api;
			if (api == "DX12") {
				moduleName = "RendererDX12";
			}
			else if (api == "DX11") {
				moduleName = "RendererDX11";
			}
			else {
				assert(0);
				return false;
			}
			pluginInitializer = platform::LoadModule<PI_GraphicsInitializer>(moduleName, "Initialize");
			pluginDeinitializer = platform::LoadModule<PI_GraphicsDeinitializer>(moduleName, "Deinitialize");
			pluginGetDev = platform::LoadModule<PI_GetGraphicsDevice>(moduleName, "GetGraphicsDevice");
			pluginNewGRenderPath3D = platform::LoadModule<PI_NewGRenderPath3D>(moduleName, "NewGRenderPath");
			pluginNewGScene = platform::LoadModule<PI_NewGScene>(moduleName, "NewGScene");
			pluginInitRenderer = platform::LoadModule<PI_InitRenderer>(moduleName, "InitRenderer");
			pluginLoadShader = platform::LoadModule<PI_LoadShader>(moduleName, "LoadShader");
			pluginLoadShaders = platform::LoadModule<PI_LoadShaders>(moduleName, "LoadShaders");

			pluginAddDeferredMIPGen = platform::LoadModule<PI_AddDeferredMIPGen>(moduleName, "AddDeferredMIPGen");
			pluginAddDeferredBlockCompression = platform::LoadModule<PI_AddDeferredBlockCompression>(moduleName, "AddDeferredBlockCompression");
			pluginAddDeferredTextureCopy = platform::LoadModule<PI_AddDeferredTextureCopy>(moduleName, "AddDeferredTextureCopy");
			pluginAddDeferredBufferUpdate = platform::LoadModule<PI_AddDeferredBufferUpdate>(moduleName, "AddDeferredBufferUpdate");

			return pluginInitializer && pluginDeinitializer && pluginGetDev && pluginNewGRenderPath3D && pluginNewGScene && pluginInitRenderer && pluginLoadShader && pluginLoadShaders
				&& pluginAddDeferredTextureCopy && pluginAddDeferredBufferUpdate;
		}
	};
}