#pragma once
#include "RenderPath.h"
#include "Components/Components.h"

#include <vector>
#include <string>

namespace vz
{
	class RenderPath2D :
		public RenderPath
	{
	protected:
		void tryResizeRenderTargets() override;
	public:
		// to do ... slicer component...

		void Render() const override;
	};

}
