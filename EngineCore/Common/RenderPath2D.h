#pragma once
#include "RenderPath.h"

#include <vector>
#include <string>

namespace vz
{
	class RenderPath2D :
		public RenderPath
	{
		using SlicerComponent = CameraComponent;
	protected:
		graphics::Texture rtRender2D_;

		uint32_t msaaSampleCount_ = 1;
	public:
		RenderPath2D(const Entity entity, graphics::GraphicsDevice* graphicsDevice) 
			: RenderPath(entity, graphicsDevice) {}
		~RenderPath2D() { DeleteGPUResources(); }
		// to do ... slicer component...

		SlicerComponent* slicer = nullptr;

		void DeleteGPUResources() override;
		void ResizeResources() override;
		void Update(const float dt) override;
		void Render() override;
	};

}
