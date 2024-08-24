#pragma once
#include "RenderPath2D.h"

namespace vz
{
	class RenderPath3D :
		public RenderPath2D
	{
	protected:

	public:
		void Render() const override;
	};
}
