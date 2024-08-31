#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzRenderer : VzBaseComp
	{
		VzRenderer(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, "VzRenderer") {}
		void SetCanvas(const uint32_t w, const uint32_t h, const float dpi, void* window = nullptr);
		void GetCanvas(uint32_t* w, uint32_t* h, float* dpi, void** window = nullptr);

		void SetViewport(const uint32_t x, const uint32_t y, const uint32_t w, const uint32_t h);
		void GetViewport(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h);

		void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits);

		VZRESULT Render(const VID vidScene, const VID vidCam);
		//VZRESULT Render(const VzBaseComp* scene, const VzBaseComp* camera) { return Render(scene->GetVID(), camera->GetVID()); };
	};
}
