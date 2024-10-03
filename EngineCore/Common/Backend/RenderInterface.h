#pragma once
#include "GBackendDevice.h"

#include <memory>
#include <limits>

namespace vz
{
	//namespace renderer
	//{
	//	// Add a texture that should be mipmapped whenever it is feasible to do so
	//	void AddDeferredMIPGen(const vz::graphics::Texture& texture, bool preserve_coverage = false);
	//	void AddDeferredBlockCompression(const vz::graphics::Texture& texture_src, const vz::graphics::Texture& texture_bc);
	//
	//	bool LoadShader(
	//		vz::graphics::ShaderStage stage,
	//		vz::graphics::Shader& shader,
	//		const std::string& filename,
	//		vz::graphics::ShaderModel minshadermodel = vz::graphics::ShaderModel::SM_6_0,
	//		const std::vector<std::string>& permutation_defines = {}
	//	);
	//}

	struct Scene;
	struct GRenderPath3D
	{
		inline static const std::string GRenderPath3D_INTERFACE_VERSION = "GRenderPath3D::20241001";
		// this will be a component of vz::RenderPath3D
	protected:
		graphics::SwapChain& swapChain_;
		graphics::Texture& rtRenderFinal_;
	public:
		std::string version = GRenderPath3D_INTERFACE_VERSION;

		GRenderPath3D(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal) : swapChain_(swapChain), rtRenderFinal_(rtRenderFinal) {}

		virtual bool ResizeCanvas() = 0; // must delete all canvas-related resources and re-create
		virtual bool Render() = 0;
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
		typedef GRenderPath3D* (*PI_NewGRenderPath3D)(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal);
		typedef GScene* (*PI_NewGScene)(Scene* scene);
		PI_GraphicsInitializer graphicsInitializer = nullptr;
		PI_GraphicsDeinitializer graphicsDeinitializer = nullptr;
		PI_GetGraphicsDevice graphicsGetDev = nullptr;
		PI_NewGRenderPath3D graphicsNewGRenderPath3D = nullptr;
		PI_NewGScene graphicsNewGScene = nullptr;

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
				return false;
			}
			//vz::renderer::SetShaderPath(wi::renderer::GetShaderPath() + "hlsl6/");
			graphicsInitializer = vz::platform::LoadModule<PI_GraphicsInitializer>(moduleName, "Initialize");
			graphicsDeinitializer = vz::platform::LoadModule<PI_GraphicsDeinitializer>(moduleName, "Deinitialize");
			graphicsGetDev = vz::platform::LoadModule<PI_GetGraphicsDevice>(moduleName, "GetGraphicsDevice");
			graphicsNewGRenderPath3D = platform::LoadModule<PI_NewGRenderPath3D>(moduleName, "NewGRenderPath");
			graphicsNewGScene = platform::LoadModule<PI_NewGScene>(moduleName, "NewGScene");

			return graphicsInitializer && graphicsDeinitializer && graphicsGetDev && graphicsNewGRenderPath3D && graphicsNewGScene;
		}
	};
}