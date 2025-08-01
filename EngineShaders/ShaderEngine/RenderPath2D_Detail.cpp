#include "RenderPath3D_Detail.h"
#include "Image.h"
#include "Components/GComponents.h"

namespace vz
{
	// ----------------------------------------------------
	//  No additional buffers allocated for GRenderPath2D 
	//		as it only handles simple 2D 
	// ----------------------------------------------------

	Entity entityLoadingTexture = INVALID_ENTITY;

	bool GRenderPath2D::ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight)
	{
		// no additional resources
		return true;
	}
	bool GRenderPath2D::Render2D(const float dt)
	{
		if (!image::IsInitialized())
		{
			return false;
		}
		if (!renderer::IsInitialized())
		{
			auto range = profiler::BeginRangeCPU("Loading Screen");

			image::SetCanvas(canvasWidth_, canvasHeight_, 96.F);

			image::Params fx;
			fx.blendFlag = MaterialComponent::BlendMode::BLENDMODE_OPAQUE;
			fx.quality = image::QUALITY_LINEAR;
			fx.enableBackground();
			fx.enableFullScreen();
			
			graphics::CommandList cmd = device->BeginCommandList();
			device->EventBegin("Loading Screen", cmd);

			if (rtRenderFinal_.IsValid())
			{
				graphics::RenderPassImage rp[] = {
					graphics::RenderPassImage::RenderTarget(&rtRenderFinal_, graphics::RenderPassImage::LoadOp::LOAD), //CLEAR
				};
				device->RenderPassBegin(rp, arraysize(rp), cmd);
			}
			else
			{
				device->RenderPassBegin(&swapChain_, cmd);
			}

			if (!compfactory::ContainTextureComponent(entityLoadingTexture))
			{
				entityLoadingTexture = compfactory::MakeResTexture("ShaderEngine Loading Texture");
			}
			GTextureComponent* texture = (GTextureComponent*)compfactory::GetTextureComponent(entityLoadingTexture);
			if (!texture->HasRenderData())
			{
				texture->LoadImageFile("../Assets/testimage_2ns.jpg");
			}

			// Begin final compositing:
			graphics::Viewport viewport_composite; // full buffer
			viewport_composite.width = (float)canvasWidth_;
			viewport_composite.height = (float)canvasHeight_;
			device->BindViewports(1, &viewport_composite, cmd);
			image::Draw(&texture->resource.GetTexture(), fx, cmd);

			device->RenderPassEnd(cmd);
			
			device->EventEnd(cmd);

			profiler::EndRange(range);
		}
		return true;
	}
	bool GRenderPath2D::Destroy()
	{
		// no additional resources
		return true;
	}
}