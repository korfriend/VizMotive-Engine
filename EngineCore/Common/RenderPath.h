#pragma once
#include "Canvas.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"
#include "Utils/Timer.h"
#include "Backend/GBackendDevice.h"

namespace vz
{
	class RenderPath : public Canvas
	{
	protected:
		graphics::ColorSpace colorSpace_ = graphics::ColorSpace::SRGB;
		uint32_t msaaSampleCount_ = 1;

		graphics::GraphicsDevice* graphicsDevice_ = nullptr;
		// swapChain_ and rtRenderFinal_ are exclusive!
		graphics::SwapChain swapChain_;	// handled in RenderPath2D
		graphics::Texture rtRenderFinal_; // handled in RenderPath2D
		graphics::GPUResource resShared_;
		uint64_t sharedHandleDescriptorPtr_ = 0u;


		// resize check variables
		uint32_t prevWidth_ = 0;
		uint32_t prevHeight_ = 0;
		float prevDpi_ = 96;
		graphics::ColorSpace prevColorSpace_ = colorSpace_;
		uint32_t prevMsaaSampleCount_ = 1;


	public:
		RenderPath(const Entity entity, graphics::GraphicsDevice* graphicsDevice)
			: Canvas(entity), graphicsDevice_(graphicsDevice) {
			type_ = "RenderPath";
		}
		virtual ~RenderPath() = default;

		float clearColor[4] = {};

		// framerate controllers
		uint64_t frameCount = 0;
		float deltaTimeAccumulator = 0;
		float targetFrameRate = 60;
		bool frameskip = false;
		bool framerateLock = false;
		vz::Timer timer;

		bool UpdateResizedCanvas() {
			if (swapChain_.IsValid())
			{
				colorSpace_ = graphicsDevice_->GetSwapChainColorSpace(&swapChain_);
			}
			if (prevWidth_ != width_ || prevHeight_ != height_ ||
				prevDpi_ != dpi_ || prevColorSpace_ != colorSpace_ ||
				prevMsaaSampleCount_ != msaaSampleCount_)
			{
				prevWidth_ = width_; prevHeight_ = height_; prevDpi_ = dpi_; prevColorSpace_ = colorSpace_;
				prevMsaaSampleCount_ = msaaSampleCount_;
				return true;
			}
			return false;
		};

		//const graphics::Texture& GetFinalRenderTarget() const { return rtRenderFinal_; }
		//const graphics::SwapChain& GetSwapChain() const { return swapChain_; }
		bool GetSharedRendertargetView(const void* device2, const void* srvDescHeap2, const int descriptorIndex, uint64_t& descriptorHandle, void** resPtr);

		virtual void DeleteGPUResources(const bool resizableOnly) = 0;
		virtual void ResizeResources() = 0;
		virtual void Update(const float dt) = 0;
		virtual void Render(const float dt) = 0;
		virtual void Compose() = 0;
	};
}
