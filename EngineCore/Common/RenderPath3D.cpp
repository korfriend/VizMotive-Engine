#include "RenderPath3D.h"

extern GEngineConfig gEngine;

namespace vz
{
	namespace graphics
	{
		struct GRenderPath3D
		{
			inline static const std::string GRenderPath3D_INTERFACE_VERSION = "GRenderPath3D::20240921";
			// this will be a component of vz::RenderPath3D
		protected:
			RenderPath3D* renderPath3D_ = nullptr;
		public:
			std::string version = GRenderPath3D_INTERFACE_VERSION;

			GRenderPath3D(RenderPath3D* renderPath) : renderPath3D_(renderPath) {}

			virtual bool ResizeCanvas() = 0; // must delete all canvas-related resources and re-create
			virtual bool Render() = 0;
			virtual bool Destory() = 0;
		};
	}

	using namespace graphics;

	typedef GRenderPath3D* (*PI_NewGRenderPath3D)(RenderPath3D* renderPath);
	PI_NewGRenderPath3D graphicsNewGRenderPath3D = nullptr;

	RenderPath3D::RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice)
		: RenderPath2D(entity, graphicsDevice_) 
	{
		if (graphicsNewGRenderPath3D == nullptr)
		{
			if (gEngine.api == "DX12")
			{
				graphicsNewGRenderPath3D = platform::LoadModule<PI_NewGRenderPath3D>("RendererDX12", "NewGRenderPath");
			}
		}
		assert(graphicsNewGRenderPath3D);
		handlerRenderPath3D_ = graphicsNewGRenderPath3D(this);
		assert(handlerRenderPath3D_->version == GRenderPath3D::GRenderPath3D_INTERFACE_VERSION);
	}

	void RenderPath3D::DeleteGPUResources()
	{
		RenderPath2D::DeleteGPUResources();
		rtRenderInterResult_ = {};
		rtRender3D_ = {};
		if (handlerRenderPath3D_)
		{
			handlerRenderPath3D_->Destory();
			delete handlerRenderPath3D_;
			handlerRenderPath3D_ = nullptr;
		}
	}

	void RenderPath3D::ResizeResources()
	{
		DeleteGPUResources();
		if (!rtRender3D_.IsValid())
		{
			TextureDesc desc;
			desc.width = width_;
			desc.height = height_;
			desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE | BindFlag::UNORDERED_ACCESS;
			desc.format = formatRendertargetMain;
			assert(graphicsDevice_->CreateTexture(&desc, nullptr, &rtRender3D_));
			graphicsDevice_->SetName(&rtRender3D_, std::string("rtRender3D_" + std::to_string(entity_)).c_str());
		}
		handlerRenderPath3D_->ResizeCanvas();
		RenderPath2D::ResizeResources();
	}

	void RenderPath3D::Update(const float dt)
	{
		RenderPath2D::Update(dt);

		if (camera == nullptr || scene == nullptr) return;
		scene->Update(dt);
	}

	void RenderPath3D::Render()
	{
		if (IsCanvasResized() || 
			rtRender3D_.desc.sample_count != msaaSampleCount_)
		{
			ResizeResources(); // call RenderPath2D::ResizeResources();
			// since IsCanvasResized() updates the canvas size,
			//	resizing sources does not happen redundantly 
		}
		RenderPath2D::Render();

		if (camera == nullptr || scene == nullptr)
		{
			backlog::post("RenderPath3D::Render >> No Scene or No Camera in RenderPath3D!", backlog::LogLevel::Warn);
			return;
		}

		// RenderPath3D code //
		if (camera->IsDirty())
		{
			camera->UpdateMatrix();
		}

		handlerRenderPath3D_->Render();

	}
}