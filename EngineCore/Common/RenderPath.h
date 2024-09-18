#pragma once
#include "Canvas.h"

namespace vz
{
	class RenderPath : public Canvas
	{
	protected:
		uint32_t prevWidth_ = 0;
		uint32_t prevHeight_ = 0;
		float prevDpi_ = 96;

		virtual void tryResizeRenderTargets() = 0;
	public:
		virtual ~RenderPath() = default;

		virtual void Render() const = 0;
	};
}
