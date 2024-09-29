#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzRenderer : VzBaseComp
	{
		struct ClearOptions {
			float clearColor[4] = {};
			uint8_t clearStencil = 0u;
			bool clear = false;
			bool discard = true;
		} clearOption;

		VzRenderer(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, "VzRenderer") {}
		void SetCanvas(const uint32_t w, const uint32_t h, const float dpi, void* window = nullptr);
		void GetCanvas(uint32_t* VZ_NULLABLE w, uint32_t* VZ_NULLABLE h, float* VZ_NULLABLE dpi, void** VZ_NULLABLE window = nullptr);

		void SetViewport(const float x, const float y, const float w, const float h);
		void GetViewport(float* VZ_NULLABLE x, float* VZ_NULLABLE y, float* VZ_NULLABLE w, float* VZ_NULLABLE h);

		void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits);

		bool Render(const VID vidScene, const VID vidCam);
		bool Render(const VzBaseComp* scene, const VzBaseComp* camera) { return Render(scene->GetVID(), camera->GetVID()); };
	};
}
