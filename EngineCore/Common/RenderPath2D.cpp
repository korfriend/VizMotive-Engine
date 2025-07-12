#include "RenderPath2D.h"
#include "GBackend/GModuleLoader.h"
#include "Common/Initializer.h"

namespace vz
{
	using namespace graphics;

	bool RenderPath::GetSharedRendertargetView(const void* device2, const void* srvDescHeap2, const int descriptorIndex, uint64_t& descriptorHandle, void** resPtr)
	{
		if (!rtRenderFinal_.IsValid())
		{
			return false;
		}
		if (!resShared_.IsValid())
		{
			if (!graphicsDevice_->OpenSharedResource(device2, srvDescHeap2, descriptorIndex, &rtRenderFinal_,
				sharedHandleDescriptorPtr_, resShared_, &resPtr_))
			{
				vzlog_error("Failure to OpenSharedResource!");
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
	}

	void RenderPath2D::Update(const float dt)
	{
		// to do
	}
	void RenderPath2D::Render(const float dt)
	{
		// Renders 2D elements over the 3D rendering result (targeting rtRenderFinal or swapChain)
		//   1. Primarily used to display loading screens when the shaderEngine is not initialized
		//   2. Provides simple 2D drawing capabilities on top of 3D rendered content
		handlerRenderPath2D_->Render2D(dt);
	}
}