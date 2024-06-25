#pragma once
#include "CommonInclude.h"
#include "Backend/GBackendDevice.h"
#include "Canvas.h"

namespace vz
{
	class RenderPath : public vz::Canvas
	{
	private:
		uint32_t layerMask_ = 0xFFFFFFFF;

	public:
		virtual ~RenderPath() = default;

		// load resources in background (for example behind loading screen)
		virtual void Load() {}
		// called when RenderPath gets activated
		virtual void Start() {}
		// called when RenderPath gets deactivated (for example when switching to an other RenderPath)
		virtual void Stop() {}
		// executed before Update()
		virtual void PreUpdate() {}
		// update once per frame
		//	dt : elapsed time since last call in seconds
		virtual void Update(float dt) {}
		// executed after Update()
		virtual void PostUpdate() {}
		// Render to layers, rendertargets, etc
		// This will be rendered offscreen
		virtual void Render() const {}
		// Compose the rendered layers (for example blend the layers together as Images)
		// This will be rendered to the backbuffer
		virtual void Compose(vz::graphics::CommandList cmd) const {}

		inline uint32_t GetLayerMask() const { return layerMask_; }
		inline void SetlayerMask(uint32_t value) { layerMask_ = value; }

		vz::graphics::ColorSpace colorSpace = vz::graphics::ColorSpace::SRGB;
	};
}
