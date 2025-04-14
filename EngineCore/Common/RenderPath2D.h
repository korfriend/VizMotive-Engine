#pragma once
#include "RenderPath.h"

#include <vector>
#include <string>

namespace vz
{
	struct GRenderPath2D;
	class RenderPath2D :
		public RenderPath
	{
	protected:
		GRenderPath2D* handlerRenderPath2D_ = nullptr;

		graphics::Rect scissor_;
		uint32_t msaaSampleCount_ = 1;
		uint32_t msaaSampleCount2DOnly_ = 1;

	public:
		RenderPath2D(const Entity entity, graphics::GraphicsDevice* graphicsDevice) 
			: RenderPath(entity, graphicsDevice) {  type_ = "RenderPath2D"; }
		virtual ~RenderPath2D() { DeleteGPUResources(false); }

		void SetMSAASampleCount(uint32_t value) { msaaSampleCount_ = value; }
		constexpr uint32_t GetMSAASampleCount() const { return msaaSampleCount_; }
		
		virtual void SetMSAASampleCount2D(uint32_t value) { msaaSampleCount2DOnly_ = value; }
		constexpr uint32_t GetMSAASampleCount2D() const { return msaaSampleCount2DOnly_; }

		void DeleteGPUResources(const bool resizableOnly) override;
		void ResizeResources() override;
		void Update(const float dt) override;
		void Render(const float dt) override;
	};

}
