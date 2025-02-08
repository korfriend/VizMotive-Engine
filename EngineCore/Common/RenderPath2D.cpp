#include "RenderPath2D.h"

namespace vz
{
	using namespace graphics;

	bool RenderPath::GetSharedRendertargetView(const void* device2, const void* srvDescHeap2, const int descriptorIndex, uint64_t& descriptorHandle, void** resPtr)
	{
		if (!resShared_.IsValid())
		{
			if (!graphicsDevice_->OpenSharedResource(device2, srvDescHeap2, descriptorIndex, &rtRenderFinal_,
				sharedHandleDescriptorPtr_, resShared_, &resPtr_))
			{
				backlog::post("Failure to OpenSharedResource!", backlog::LogLevel::Error);
				return false;
			}
		}
		if (resPtr) *resPtr = resPtr_;
		descriptorHandle = sharedHandleDescriptorPtr_;
		return (void*)sharedHandleDescriptorPtr_;
	}
}

namespace vz
{
	void RenderPath2D::DeleteGPUResources(const bool resizableOnly)
	{
		//swapChain_ = {}; // this causes a crash!
		rtRenderFinal_ = {};
		resShared_ = {};
		sharedHandleDescriptorPtr_ = 0u;
		if (!resizableOnly)
		{
		}
	}

	void RenderPath2D::ResizeResources()
	{
		RenderPath2D::DeleteGPUResources(true);

		SwapChainDesc desc;
		if (window_)
		{
			if (swapChain_.IsValid())
			{
				// it will only resize, but keep format and other settings
				desc = swapChain_.desc;
			}
			else
			{
				// initialize for the first time
				desc.buffer_count = 3;
				if (graphicsDevice_->CheckCapability(GraphicsDeviceCapability::R9G9B9E5_SHAREDEXP_RENDERABLE))
				{
					desc.format = Format::R9G9B9E5_SHAREDEXP;
				}
				else
				{
					desc.format = Format::R10G10B10A2_UNORM;
				}
			}
			desc.vsync = true;
			desc.width = width_;
			desc.height = height_;
			desc.allow_hdr = allowHDR;
			bool success = graphicsDevice_->CreateSwapChain(&desc, (vz::platform::window_type)window_, &swapChain_);
			assert(success);
		}
		else
		{
			if (!rtRenderFinal_.IsValid())
			{
				TextureDesc desc;
				desc.width = width_;
				desc.height = height_;
				desc.bind_flags = BindFlag::RENDER_TARGET | BindFlag::SHADER_RESOURCE;
				// we assume the main GUI and engine use the same GPU device
				// graphics::ResourceMiscFlag::SHARED_ACROSS_ADAPTER;
				desc.misc_flags = ResourceMiscFlag::SHARED;
				desc.format = Format::R10G10B10A2_UNORM;
				bool success = graphicsDevice_->CreateTexture(&desc, nullptr, &rtRenderFinal_);
				assert(success);

				graphicsDevice_->SetName(&rtRenderFinal_, ("RenderPath::rtRenderFinal_" + std::to_string(entity_)).c_str());
			}
		}

		//if (slicer != nullptr)
		{
			//if (!rtRender2D_.IsValid())
			//{
			//	graphics::TextureDesc desc;
			//	desc.width = width_;
			//	desc.height = height_;
			//	desc.bind_flags = graphics::BindFlag::RENDER_TARGET | graphics::BindFlag::SHADER_RESOURCE;
			//	desc.format = graphics::Format::R8G8B8A8_UNORM;
			//	assert(graphicsDevice_->CreateTexture(&desc, nullptr, &rtRender2D_));
			//	graphicsDevice_->SetName(&rtRender2D_, std::string("rtRender2D_" + std::to_string(entity_)).c_str());
			//}
		}
	}

	void RenderPath2D::Update(const float dt)
	{
		// to do
	}
	void RenderPath2D::Render(const float dt)
	{
		// to do
	}
}