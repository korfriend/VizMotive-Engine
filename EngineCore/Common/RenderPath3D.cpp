#include "RenderPath3D.h"

extern GEngineConfig gEngine;

namespace vz
{
	namespace graphics
	{
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
	}

	using namespace graphics;

	typedef GRenderPath3D* (*PI_NewGRenderPath3D)(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal);
	PI_NewGRenderPath3D graphicsNewGRenderPath3D = nullptr;

	RenderPath3D::RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice)
		: RenderPath2D(entity, graphicsDevice) 
	{
		if (graphicsNewGRenderPath3D == nullptr)
		{
			if (gEngine.api == "DX12")
			{
				graphicsNewGRenderPath3D = platform::LoadModule<PI_NewGRenderPath3D>("RendererDX12", "NewGRenderPath");
			}
			else if (gEngine.api == "DX11")
			{
				graphicsNewGRenderPath3D = platform::LoadModule<PI_NewGRenderPath3D>("RendererDX11", "NewGRenderPath");
			}
		}
		assert(graphicsNewGRenderPath3D);
		handlerRenderPath3D_ = graphicsNewGRenderPath3D(swapChain_, rtRenderFinal_);
		assert(handlerRenderPath3D_->version == GRenderPath3D::GRenderPath3D_INTERFACE_VERSION);

		type_ = "RenderPath3D";
	}

	RenderPath3D::~RenderPath3D() 
	{ 
		DeleteGPUResources(false); 

		if (handlerRenderPath3D_)
		{
			handlerRenderPath3D_->Destory();
			delete handlerRenderPath3D_;
			handlerRenderPath3D_ = nullptr;
		}
	}

	void RenderPath3D::DeleteGPUResources(const bool resizableOnly)
	{
		RenderPath2D::DeleteGPUResources(resizableOnly);
		if (!resizableOnly)
		{
			//
		}
	}

	void RenderPath3D::ResizeResources()
	{
		DeleteGPUResources(true);
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
		if (camera == nullptr || scene == nullptr)
		{
			backlog::post("RenderPath3D::Render >> No Scene or No Camera in RenderPath3D!", backlog::LogLevel::Warn);
			return;
		}

		if (UpdateResizedCanvas())
		{
			ResizeResources(); // call RenderPath2D::ResizeResources();
			// since IsCanvasResized() updates the canvas size,
			//	resizing sources does not happen redundantly 
		}
		RenderPath2D::Render();

		// RenderPath3D code //
		// Camera Updates
		{
			HierarchyComponent* hier = compfactory::GetHierarchyComponent(camera->GetEntity());
			if (hier->GetParentEntity() != INVALID_ENTITY)
			{
				camera->SetWorldLookAtFromHierarchyTransforms();
			}
			if (camera->IsDirty())
			{
				camera->UpdateMatrix();
			}
		}

		// Clear Option //
		if (swapChain_.IsValid())
		{
			memcpy(swapChain_.desc.clear_color, clearColor, sizeof(float) * 4);
		}
		else
		{
			assert(rtRenderFinal_.IsValid());
			memcpy(rtRenderFinal_.desc.clear.color, clearColor, sizeof(float) * 4);
		}
		
		handlerRenderPath3D_->Render();
	}
}