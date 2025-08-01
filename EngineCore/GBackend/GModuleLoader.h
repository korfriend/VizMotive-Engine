#pragma once
#include "GShaderInterface.h"

#include <memory>
#include <limits>

namespace vz
{
	using Entity = uint64_t;

	extern std::unordered_map<std::string, HMODULE> importedModules;

	struct GBackendLoader
	{
		typedef bool(*PI_GraphicsInitializer)(graphics::ValidationMode validationMode, graphics::GPUPreference preference);
		typedef vz::graphics::GraphicsDevice* (*PI_GetGraphicsDevice)();
		typedef bool(*PI_GraphicsDeinitializer)();

		PI_GraphicsInitializer pluginInitializer = nullptr;
		PI_GraphicsDeinitializer pluginDeinitializer = nullptr;
		PI_GetGraphicsDevice pluginGetDev = nullptr;

		std::string API = "";
		std::string moduleName = "";

		bool Init(const std::string& api)
		{
			API = api;
			if (api == "DX12") {
				moduleName = "GBackendDX12";
			}
			else if (api == "DX11") {
				moduleName = "GBackendDX11";
			}
			else if (api == "VULKAN") {
				moduleName = "GBackendVulkan";
			}
			else {
				assert(0);
				return false;
			}
			pluginInitializer = platform::LoadModule<PI_GraphicsInitializer>(moduleName, "Initialize", importedModules);
			pluginDeinitializer = platform::LoadModule<PI_GraphicsDeinitializer>(moduleName, "Deinitialize", importedModules);
			pluginGetDev = platform::LoadModule<PI_GetGraphicsDevice>(moduleName, "GetGraphicsDevice", importedModules);

			return pluginInitializer && pluginDeinitializer && pluginGetDev;
		}
	};

	struct GShaderEngineLoader
	{
		typedef bool(*PI_Initializer)(graphics::GraphicsDevice* device);
		typedef bool(*PI_Deinitializer)();

		typedef bool(*PI_LoadRenderer)();
		typedef bool(*PI_ApplyConfiguration)();

		typedef GRenderPath3D* (*PI_NewGRenderPath3D)(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal);
		typedef GScene* (*PI_NewGScene)(Scene* scene);
		typedef void(*PI_AddDeferredMIPGen)(const graphics::Texture& texture, bool preserve_coverage);
		typedef void(*PI_AddDeferredBlockCompression)(const graphics::Texture& texture_src, const graphics::Texture& texture_bc);
		typedef void(*PI_AddDeferredTextureCopy)(const graphics::Texture& texture_src, const graphics::Texture& texture_dst, const bool mipGen);
		typedef void(*PI_AddDeferredBufferUpdate)(const graphics::GPUBuffer& buffer, const void* data, const uint64_t size, const uint64_t offset);
		typedef void(*PI_AddDeferredGeometryGPUBVHUpdate)(const Entity entity);

		typedef bool(*PI_LoadShader)(
			graphics::ShaderStage stage,
			graphics::Shader& shader,
			const std::string& filename,
			graphics::ShaderModel minshadermodel,
			const std::vector<std::string>& permutation_defines);
		typedef bool(*PI_LoadShaders)();

		// essential
		PI_Initializer pluginInitializer = nullptr;
		PI_Deinitializer pluginDeinitializer = nullptr;
		PI_LoadRenderer pluginLoadRenderer = nullptr;
		PI_ApplyConfiguration pluginApplyConfiguration = nullptr;
		PI_NewGRenderPath3D pluginNewGRenderPath3D = nullptr;
		PI_NewGScene pluginNewGScene = nullptr;
		PI_LoadShader pluginLoadShader = nullptr;
		PI_LoadShaders pluginLoadShaders = nullptr;

		PI_AddDeferredTextureCopy pluginAddDeferredTextureCopy = nullptr;
		PI_AddDeferredBufferUpdate pluginAddDeferredBufferUpdate = nullptr;
		PI_AddDeferredGeometryGPUBVHUpdate pluginAddDeferredGeometryGPUBVHUpdate = nullptr;

		// optional 
		PI_AddDeferredMIPGen pluginAddDeferredMIPGen = nullptr;
		PI_AddDeferredBlockCompression pluginAddDeferredBlockCompression = nullptr;

		std::string moduleName = "";

		bool Init(const std::string& shaderModuleName)
		{
			moduleName = shaderModuleName;
			
			pluginInitializer = platform::LoadModule<PI_Initializer>(moduleName, "Initialize", importedModules);
			pluginDeinitializer = platform::LoadModule<PI_Deinitializer>(moduleName, "Deinitialize", importedModules);

			pluginLoadRenderer = platform::LoadModule<PI_LoadRenderer>(moduleName, "LoadRenderer", importedModules);
			pluginApplyConfiguration = platform::LoadModule<PI_ApplyConfiguration>(moduleName, "ApplyConfiguration", importedModules);
			
			pluginNewGRenderPath3D = platform::LoadModule<PI_NewGRenderPath3D>(moduleName, "NewGRenderPath", importedModules);
			pluginNewGScene = platform::LoadModule<PI_NewGScene>(moduleName, "NewGScene", importedModules);
			pluginLoadShader = platform::LoadModule<PI_LoadShader>(moduleName, "LoadShader", importedModules);
			pluginLoadShaders = platform::LoadModule<PI_LoadShaders>(moduleName, "LoadShaders", importedModules);

			pluginAddDeferredMIPGen = platform::LoadModule<PI_AddDeferredMIPGen>(moduleName, "AddDeferredMIPGen", importedModules);
			pluginAddDeferredBlockCompression = platform::LoadModule<PI_AddDeferredBlockCompression>(moduleName, "AddDeferredBlockCompression", importedModules);
			pluginAddDeferredTextureCopy = platform::LoadModule<PI_AddDeferredTextureCopy>(moduleName, "AddDeferredTextureCopy", importedModules);
			pluginAddDeferredBufferUpdate = platform::LoadModule<PI_AddDeferredBufferUpdate>(moduleName, "AddDeferredBufferUpdate", importedModules);
			pluginAddDeferredGeometryGPUBVHUpdate = platform::LoadModule<PI_AddDeferredGeometryGPUBVHUpdate>(moduleName, "AddDeferredGeometryGPUBVHUpdate", importedModules);

			return pluginInitializer && pluginDeinitializer && pluginLoadRenderer && pluginApplyConfiguration && pluginNewGRenderPath3D && pluginNewGScene && pluginLoadShader && pluginLoadShaders
				&& pluginAddDeferredTextureCopy && pluginAddDeferredBufferUpdate && pluginAddDeferredGeometryGPUBVHUpdate;
		}
	};
}