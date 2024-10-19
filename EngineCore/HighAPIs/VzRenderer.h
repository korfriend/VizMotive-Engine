#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzRenderer : VzBaseComp
	{
		VzRenderer(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::RENDERER) {}
		void SetCanvas(const uint32_t w, const uint32_t h, const float dpi, void* window = nullptr);
		void GetCanvas(uint32_t* VZ_NULLABLE w, uint32_t* VZ_NULLABLE h, float* VZ_NULLABLE dpi, void** VZ_NULLABLE window = nullptr);

		void SetViewport(const float x, const float y, const float w, const float h);
		void GetViewport(float* VZ_NULLABLE x, float* VZ_NULLABLE y, float* VZ_NULLABLE w, float* VZ_NULLABLE h);
		void UseCanvasViewport();
		// add scissor interfaces

		void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits);

		void SetClearColor(const float color[4]);
		void GetClearColor(float color[4]) const;

		void SetAllowHDR(const bool enable);
		bool GetAllowHDR() const;

		bool Render(const VID vidScene, const VID vidCam);
		bool Render(const VzBaseComp* scene, const VzBaseComp* camera) { return Render(scene->GetVID(), camera->GetVID()); };
	};
}
