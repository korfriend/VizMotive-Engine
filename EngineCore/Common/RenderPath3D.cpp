#include "RenderPath3D.h"

// calls... 

namespace vz
{
	namespace graphics
	{
		class GRenderPath3D
		{
			inline static const std::string GRenderPath3D_INTERFACE_VERSION = "20240921";
			// this will be a component of vz::RenderPath3D
		protected:
			RenderPath3D* renderPath3D_ = nullptr;
		public:
			GRenderPath3D(RenderPath3D* renderPath) : renderPath3D_(renderPath) {}
			~GRenderPath3D() { Destory(); }

			virtual bool ResizeCanvas() = 0; // must delete all canvas-related resources and re-create
			virtual bool Render(const float dt) = 0;
			virtual bool Destory() = 0;
		};
	}

	using namespace graphics;

	RenderPath3D::RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice)
		: RenderPath2D(entity, graphicsDevice_) 
	{

	}

	void RenderPath3D::DeleteGPUResources()
	{
		RenderPath2D::DeleteGPUResources();
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

		RenderPath2D::ResizeResources();
	}

	void RenderPath3D::Update(const float dt)
	{
		if (rtRender3D_.desc.sample_count != msaaSampleCount_)
		{
			ResizeResources();
		}

		
	}
	void RenderPath3D::Render()
	{
		// to do

		RenderPath2D::Render();
	}
}