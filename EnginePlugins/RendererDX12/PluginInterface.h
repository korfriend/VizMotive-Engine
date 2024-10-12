#pragma once
#ifdef _WIN32
#define DX12_EXPORT __declspec(dllexport)
#else
#define DX12_EXPORT __attribute__((visibility("default")))
#endif
#include <string>
#include <vector>

namespace vz
{
	namespace graphics
	{
		class GraphicsDevice;
		struct SwapChain;
		struct Viewport;
		struct Texture;
		struct Shader;
		enum class ShaderStage : uint8_t;
		enum class ShaderModel : uint8_t;
	}

	struct Scene;
	struct CameraComponent;
	struct RenderPath3D;
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
	struct GRenderPath3D
	{
		inline static const std::string GRenderPath3D_INTERFACE_VERSION = "GRenderPath3D::20241012";
		// this will be a component of vz::RenderPath3D
	protected:
		graphics::Viewport& viewport_;
		graphics::SwapChain& swapChain_;
		graphics::Texture& rtRenderFinal_;

		// canvas size is supposed to be updated via ResizeCanvas(..)
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
}

namespace vz
{
	// PluginInterface.cpp
	extern "C" DX12_EXPORT bool Initialize();
	extern "C" DX12_EXPORT graphics::GraphicsDevice* GetGraphicsDevice();
	extern "C" DX12_EXPORT void Deinitialize();

	// Renderer.cpp
	extern "C" DX12_EXPORT GScene* NewGScene(Scene* scene);
	extern "C" DX12_EXPORT GRenderPath3D* NewGRenderPath(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal);

	// ShaderLoader.cpp
	extern "C" DX12_EXPORT bool LoadShader(
		graphics::ShaderStage stage,
		graphics::Shader& shader,
		const std::string& filename,
		graphics::ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines);
}