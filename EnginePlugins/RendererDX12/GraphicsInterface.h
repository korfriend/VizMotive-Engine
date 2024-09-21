#pragma once
#ifdef _WIN32
#define DX12_EXPORT __declspec(dllexport)
#else
#define DX12_EXPORT __attribute__((visibility("default")))
#endif

namespace vz
{
	struct Scene;
	struct RenderPath3D;

	namespace graphics
	{
		class DX12_EXPORT GScene
		{
			inline static const std::string GScene_INTERFACE_VERSION = "20240921";
			// this will be a component of vz::Scene
		protected:
			Scene* scene_ = nullptr;
		public:
			GScene(Scene* scene) : scene_(scene) {}
			~GScene() { Destory(); }

			virtual bool Update(const float dt) = 0;
			virtual bool Destory() = 0;
		};

		class DX12_EXPORT GRenderPath3D
		{
			inline static const std::string GRenderPath3D_INTERFACE_VERSION = "20240921";
			// this will be a component of vz::RenderPath3D
		protected:
			RenderPath3D* renderPath3D_ = nullptr;
		public:
			GRenderPath3D(RenderPath3D* renderPath) : renderPath3D_(renderPath) {}
			~GRenderPath3D() { Destory(); }

			virtual bool ResizeCanvas() = 0; // must delete all canvas-related resources and re-create
			virtual bool Render(const float dt) = 0;
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


	DX12_EXPORT GScene* NewGScene(Scene* scene);
	DX12_EXPORT GRenderPath3D* NewGRenderPath(RenderPath3D* renderPath);
}