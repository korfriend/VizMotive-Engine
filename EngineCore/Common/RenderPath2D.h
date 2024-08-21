#pragma once
#include "RenderPath.h"

#include <vector>
#include <string>

namespace vz
{
	class Sprite;
	class SpriteFont;

	class RenderPath2D :
		public RenderPath
	{
	protected:

	public:
		void Render() const override;
	};

}
