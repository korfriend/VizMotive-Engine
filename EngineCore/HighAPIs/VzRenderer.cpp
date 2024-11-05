#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Common/RenderPath3D.h"
#include "Utils/Backlog.h"
#include "Utils/Helpers.h"
#include "Utils/Profiler.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_RENDERPATH(RENDERER, RET) RenderPath3D* RENDERER = (RenderPath3D*)canvas::GetCanvas(componentVID_); \
	if (!RENDERER) {post("RenderPath3D(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;} \
	if (RENDERER->GetType() != "RenderPath3D") {post("RenderPath3D(" + to_string(componentVID_) + ") is NOT RenderPath3D!", LogLevel::Error); return RET;}

	void VzRenderer::SetCanvas(const uint32_t w, const uint32_t h, const float dpi, void* window)
	{
		GET_RENDERPATH(renderer, );
		renderer->SetCanvas(std::max(w, 1u), std::max(h, 1u), dpi, window);
		UpdateTimeStamp();
	}

	void VzRenderer::ResizeCanvas(const uint32_t w, const uint32_t h)
	{
		GET_RENDERPATH(renderer, );

		uint32_t w1 = std::max(w, 1u);
		uint32_t h1 = std::max(h, 1u);
		renderer->SetCanvas(w1, h1, renderer->GetDPI(), renderer->GetWindow());

		if (renderer->camera)
		{
			float z_near, z_far;
			renderer->camera->GetNearFar(&z_near, &z_far);
			renderer->camera->SetPerspective(
				(float)w1/(float)h1, 
				1.f, z_near, z_far, renderer->camera->GetFovVertical()
			);
		}

		UpdateTimeStamp();
	}

	void VzRenderer::GetCanvas(uint32_t* VZ_NULLABLE w, uint32_t* VZ_NULLABLE h, float* VZ_NULLABLE dpi, void** VZ_NULLABLE window)
	{
		GET_RENDERPATH(renderer, );
		if (w) *w = renderer->GetPhysicalWidth();
		if (h) *h = renderer->GetPhysicalHeight();
		if (dpi) *dpi = renderer->GetDPI();
		if (window) *window = renderer->GetWindow();
	}

	void VzRenderer::SetViewport(const float x, const float y, const float w, const float h)
	{
		GET_RENDERPATH(renderer, );
		renderer->viewport.top_left_x = x;
		renderer->viewport.top_left_y = y;
		renderer->viewport.width = w;
		renderer->viewport.height = h;
	}

	void VzRenderer::GetViewport(float* VZ_NULLABLE x, float* VZ_NULLABLE y, float* VZ_NULLABLE w, float* VZ_NULLABLE h)
	{
		GET_RENDERPATH(renderer, );
		if (x) *x = renderer->viewport.top_left_x;
		if (y) *y = renderer->viewport.top_left_y;
		if (w) *w = renderer->viewport.width;
		if (h) *h = renderer->viewport.height;
	}

	void VzRenderer::UseCanvasViewport()
	{
		GET_RENDERPATH(renderer, );

		renderer->useManualSetViewport = false;

		renderer->viewport.top_left_x = 0;
		renderer->viewport.top_left_y = 0;
		renderer->viewport.width = (float)renderer->GetPhysicalWidth();
		renderer->viewport.height = (float)renderer->GetPhysicalHeight();
	}

	void VzRenderer::SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits)
	{
		// to do //
		// add visibleMask(uint8_t) to RenderPath3D
	}

	void VzRenderer::SetClearColor(const vfloat4& color)
	{
		GET_RENDERPATH(renderer, );
		memcpy(renderer->clearColor, &color, sizeof(float) * 4);
		UpdateTimeStamp();
	}

	void VzRenderer::GetClearColor(vfloat4& color) const
	{
		GET_RENDERPATH(renderer, );
		memcpy(&color, renderer->clearColor, sizeof(float) * 4);
	}

	void VzRenderer::SetAllowHDR(const bool enable)
	{
		GET_RENDERPATH(renderer, );
		renderer->allowHDR = enable;
	}
	bool VzRenderer::GetAllowHDR() const
	{
		GET_RENDERPATH(renderer, false);
		return renderer->allowHDR;
	}

	bool VzRenderer::Render(const VID vidScene, const VID vidCam)
	{
		GET_RENDERPATH(renderer, false);

		renderer->scene = Scene::GetScene(vidScene);
		renderer->camera = compfactory::GetCameraComponent(vidCam);

		if (!renderer->scene || !renderer->camera)
		{
			return false;
		}

		float dt = float(std::max(0.0, renderer->timer.record_elapsed_seconds()));
		const float target_deltaTime = 1.0f / renderer->targetFrameRate;
		if (renderer->framerateLock && dt < target_deltaTime)
		{
			if (renderer->frameskip)
			{
				return false;
			}
			helper::QuickSleep((target_deltaTime - dt) * 1000);
			dt += float(std::max(0.0, renderer->timer.record_elapsed_seconds()));
		}
		renderer->deltaTimeAccumulator += dt;

		renderer->Update(dt);
		renderer->Render(dt);

		renderer->frameCount++;

		return true;
	}
}