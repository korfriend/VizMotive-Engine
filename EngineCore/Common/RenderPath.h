#pragma once
#include "Canvas.h"
#include "Components/Components.h"
#include "Backend/GBackendDevice.h"

namespace vz
{
	class RenderPath : public Canvas
	{
	protected:
		graphics::ColorSpace colorSpace_ = graphics::ColorSpace::SRGB;

		graphics::GraphicsDevice* graphicsDevice_ = nullptr;
		graphics::SwapChain swapChain_;	// handled in RenderPath2D

		// resize check variables
		uint32_t prevWidth_ = 0;
		uint32_t prevHeight_ = 0;
		float prevDpi_ = 96;
		graphics::ColorSpace prevColorSpace_ = colorSpace_;

		
	public:
		RenderPath(const Entity entity, graphics::GraphicsDevice* graphicsDevice) 
			: Canvas(entity), graphicsDevice_(graphicsDevice_) {}
		virtual ~RenderPath() = default;

		bool IsCanvasResized() {
			if (swapChain_.IsValid())
			{
				colorSpace_ = graphicsDevice_->GetSwapChainColorSpace(&swapChain_);
			}
			if (prevWidth_ != width_ || prevHeight_ != height_ ||
				prevDpi_ != dpi_ || prevColorSpace_ != colorSpace_)
			{
				prevWidth_ = width_; prevHeight_ = height_; prevDpi_ = dpi_; prevColorSpace_ = colorSpace_;
				return true;
			}
			return false;
		};

		virtual void DeleteGPUResources() = 0;
		virtual void ResizeResources() = 0;
		virtual void Update(const float dt) = 0;
		virtual void Render() = 0;
	};
}
