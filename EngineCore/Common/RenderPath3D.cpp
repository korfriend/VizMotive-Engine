#include "RenderPath3D.h"

// calls... 

namespace vz
{
	void RenderPath3D::DeleteGPUResources()
	{
		RenderPath2D::DeleteGPUResources();
	}

	void RenderPath3D::ResizeResources()
	{
		DeleteGPUResources();

		if (swapChain_.IsValid())
		{
			// to do
		}

		if (slicer == nullptr)
		{
			return;
		}

		if (!rtRender2D_.IsValid())
		{
			graphics::TextureDesc desc;
			desc.width = width_;
			desc.height = height_;
			desc.bind_flags = graphics::BindFlag::RENDER_TARGET | graphics::BindFlag::SHADER_RESOURCE;
			desc.format = graphics::Format::R8G8B8A8_UNORM;
			assert(graphicsDevice_->CreateTexture(&desc, nullptr, &rtRender2D_));
			graphicsDevice_->SetName(&rtRender2D_, std::string("rtRender2D_" + std::to_string(entity_)).c_str());
		}
	}

	void RenderPath3D::Update() const
	{
		// to do

		RenderPath2D::Render();
	}
	void RenderPath3D::Render() const
	{
		// to do

		RenderPath2D::Render();
	}
}