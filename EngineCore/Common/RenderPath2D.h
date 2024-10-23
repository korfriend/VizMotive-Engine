#pragma once
#include "RenderPath.h"

#include <vector>
#include <string>

namespace vz
{
	class RenderPath2D :
		public RenderPath
	{
	protected:
		uint32_t msaaSampleCount_ = 1;

	public:
		RenderPath2D(const Entity entity, graphics::GraphicsDevice* graphicsDevice) 
			: RenderPath(entity, graphicsDevice) {  type_ = "RenderPath2D"; }
		~RenderPath2D() { DeleteGPUResources(false); }
		// to do ... slicer component...

		using SlicerComponent = CameraComponent;
		SlicerComponent* slicer = nullptr;

		void SetMSAASampleCount(uint32_t value) { msaaSampleCount_ = value; }
		constexpr uint32_t GetMSAASampleCount() const { return msaaSampleCount_; }

		void DeleteGPUResources(const bool resizableOnly) override;
		void ResizeResources() override;
		void Update(const float dt) override;
		void Render(const float dt) override;
	};

}
