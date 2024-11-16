#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzRenderer : VzBaseComp
	{
		VzRenderer(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::RENDERER) {}
		void SetCanvas(const uint32_t w, const uint32_t h, const float dpi, void* window = nullptr);
		void ResizeCanvas(const uint32_t w, const uint32_t h, const CamVID vidCam = 0u); // preserves dpi and window handler
		void GetCanvas(uint32_t* VZ_NULLABLE w, uint32_t* VZ_NULLABLE h, float* VZ_NULLABLE dpi, void** VZ_NULLABLE window = nullptr);

		void SetViewport(const float x, const float y, const float w, const float h);
		void GetViewport(float* VZ_NULLABLE x, float* VZ_NULLABLE y, float* VZ_NULLABLE w, float* VZ_NULLABLE h);
		void UseCanvasViewport();
		// add scissor interfaces

		void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits);

		void SetClearColor(const vfloat4& color);
		void GetClearColor(vfloat4& color) const;

		void SetAllowHDR(const bool enable);
		bool GetAllowHDR() const;

		bool Render(const SceneVID vidScene, const CamVID vidCam);
		bool Render(const VzScene* scene, const VzCamera* camera) { return Render(scene->GetVID(), camera->GetVID()); };

		// the render target resource must be fenced before calling the next Render()
		void* GetSharedRenderTarget(const void* graphicsDev2, const void* srvDescHeap2, const int descriptorIndex, uint32_t* w, uint32_t* h);
	};
}
