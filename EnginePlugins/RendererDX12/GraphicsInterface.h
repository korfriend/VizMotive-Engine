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
	struct Scene;
	struct RenderPath3D;

	namespace graphics
	{
		struct SwapChain;
		struct Texture;
		struct Shader;
		enum class ShaderStage : uint8_t;
		enum class ShaderModel : uint8_t;

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
			inline static const std::string GRenderPath3D_INTERFACE_VERSION = "GRenderPath3D::20241001";
			// this will be a component of vz::RenderPath3D
		protected:
			SwapChain& swapChain_;
			Texture& rtRenderFinal_;
		public:
			std::string version = GRenderPath3D_INTERFACE_VERSION;

			GRenderPath3D(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal) : swapChain_(swapChain), rtRenderFinal_(rtRenderFinal) {}

			virtual bool ResizeCanvas() = 0; // must delete all canvas-related resources and re-create
			virtual bool Render() = 0;
			virtual bool Destory() = 0;
		};
	}
}

namespace vz::graphics
{
	class GraphicsDevice;

	extern "C" DX12_EXPORT bool Initialize();
	extern "C" DX12_EXPORT GraphicsDevice* GetGraphicsDevice();
	extern "C" DX12_EXPORT void Deinitialize();

	extern "C" DX12_EXPORT GScene* NewGScene(Scene* scene);
	extern "C" DX12_EXPORT GRenderPath3D* NewGRenderPath(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal);

	extern "C" DX12_EXPORT bool LoadShader(
		graphics::ShaderStage stage,
		graphics::Shader& shader,
		const std::string& filename,
		graphics::ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines);
}