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
	public:
		RenderPath2D(const Entity entity, graphics::GraphicsDevice* graphicsDevice) 
			: RenderPath(entity, graphicsDevice) {  type_ = "RenderPath2D"; }
		~RenderPath2D() { DeleteGPUResources(false); }
		// to do ... slicer component...

		using SlicerComponent = CameraComponent;
		SlicerComponent* slicer = nullptr;

		void DeleteGPUResources(const bool resizableOnly) override;
		void ResizeResources() override;
		void Update(const float dt) override;
		void Render(const float dt) override;
	};

}
