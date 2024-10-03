#include "RenderPath3D.h"
#include "Common/Backend/GRendererInterface.h"

namespace vz
{
	extern GraphicsPackage graphicsPackage;

	RenderPath3D::RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice)
		: RenderPath2D(entity, graphicsDevice) 
	{
		handlerRenderPath3D_ = graphicsPackage.pluginNewGRenderPath3D(swapChain_, rtRenderFinal_);
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