#pragma once
#include "GBackendDevice.h"

#include <memory>
#include <limits>

namespace vz
{
	struct Scene;
	struct CameraComponent;

	struct GRenderPath3D
	{
		inline static const std::string GRenderPath3D_INTERFACE_VERSION = "GRenderPath3D::20241012";
		// this will be a component of vz::RenderPath3D
	protected:
		graphics::Viewport& viewport_;
		graphics::SwapChain& swapChain_;
		graphics::Texture& rtRenderFinal_;

		// canvas size is supposed to be updated via ResizeCanvas()
		uint32_t canvasWidth_ = 1u;
		uint32_t canvasHeight_ = 1u;
	public:
		std::string version = GRenderPath3D_INTERFACE_VERSION;

		GRenderPath3D(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
			: viewport_(vp), swapChain_(swapChain), rtRenderFinal_(rtRenderFinal) {}

		Scene* scene = nullptr;
		CameraComponent* camera = nullptr;

		virtual bool ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight) = 0; // must delete all canvas-related resources and re-create
		virtual bool Render(const float dt) = 0;
		virtual bool Destory() = 0;
	};
	struct GScene
	{
		inline static const std::string GScene_INTERFACE_VERSION = "GScene::20240921";
		// this will be a component of vz::Scene
	protected:
		Scene* scene_ = nullptr;
	public:
		std::string version = GScene_INTERFACE_VERSION;

		GScene(Scene* scene) : scene_(scene) {}

		virtual bool Update(const float dt) = 0;
		virtual bool Destory() = 0;
	};
	struct GraphicsPackage
	{
		typedef bool(*PI_GraphicsInitializer)();
		typedef vz::graphics::GraphicsDevice* (*PI_GetGraphicsDevice)();
		typedef bool(*PI_GraphicsDeinitializer)();
		typedef GRenderPath3D* (*PI_NewGRenderPath3D)(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal);
		typedef GScene* (*PI_NewGScene)(Scene* scene);
		typedef void(*PI_AddDeferredMIPGen)(const graphics::Texture& texture, bool preserve_coverage);
		typedef void(*PI_AddDeferredBlockCompression)(const graphics::Texture& texture_src, const graphics::Texture& texture_bc);
		typedef bool(*PI_LoadShader)(
			graphics::ShaderStage stage,
			graphics::Shader& shader,
			const std::string& filename,
			graphics::ShaderModel minshadermodel,
			const std::vector<std::string>& permutation_defines);

		// essential
		PI_GraphicsInitializer pluginInitializer = nullptr;
		PI_GraphicsDeinitializer pluginDeinitializer = nullptr;
		PI_GetGraphicsDevice pluginGetDev = nullptr;
		PI_NewGRenderPath3D pluginNewGRenderPath3D = nullptr;
		PI_NewGScene pluginNewGScene = nullptr;
		PI_LoadShader pluginLoadShader = nullptr;

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
			pluginLoadShader = platform::LoadModule<PI_LoadShader>(moduleName, "LoadShader");

			pluginAddDeferredMIPGen = platform::LoadModule<PI_AddDeferredMIPGen>(moduleName, "AddDeferredMIPGen");
			pluginAddDeferredBlockCompression = platform::LoadModule<PI_AddDeferredBlockCompression>(moduleName, "AddDeferredBlockCompression");

			return pluginInitializer && pluginDeinitializer && pluginGetDev && pluginNewGRenderPath3D && pluginNewGScene && pluginLoadShader;
		}
	};
}