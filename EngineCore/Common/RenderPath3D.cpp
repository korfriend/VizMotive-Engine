#include "RenderPath3D.h"
#include "Common/Backend/GRendererInterface.h"
#include "Components/GComponents.h"

namespace vz
{
	extern GraphicsPackage graphicsPackage;

	RenderPath3D::RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice)
		: RenderPath2D(entity, graphicsDevice) 
	{
		handlerRenderPath3D_ = graphicsPackage.pluginNewGRenderPath3D(viewport, swapChain_, rtRenderFinal_);
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
		handlerRenderPath3D_->Destory();
		if (!resizableOnly)
		{
			// Scene?!
		}
	}

	void RenderPath3D::ResizeResources()
	{
		DeleteGPUResources(true);
		handlerRenderPath3D_->ResizeCanvas(width_, height_);
		RenderPath2D::ResizeResources();

		//viewport
		if (!useManualSetViewport)
		{
			viewport.top_left_x = 0;
			viewport.top_left_y = 0;
			viewport.width = width_;
			viewport.height = height_;
		}
		//scissor
		if (!useManualSetScissor)
		{
			scissor.left = 0;
			scissor.right = width_;
			scissor.top = 0;
			scissor.bottom = height_;
		}
	}

	// scene and camera updates
	void RenderPath3D::Update(const float dt)
	{
		RenderPath2D::Update(dt);

		if (camera == nullptr || scene == nullptr) return;

		scene->Update(dt);

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
	}
	
	void RenderPath3D::Render(const float dt)
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
		RenderPath2D::Render(dt);

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
		
		// This involves the following process
		//	1. update view (for each rendering pipeline)
		//	2. update render data
		//	3. execute rendering pipelines
		handlerRenderPath3D_->scene = scene;
		handlerRenderPath3D_->camera = camera;

		GCameraComponent* downcast_camera = (GCameraComponent*)camera;
		downcast_camera->viewport = viewport;
		downcast_camera->scissor = scissor;

		handlerRenderPath3D_->Render(dt);
	}
}