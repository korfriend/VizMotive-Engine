#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Common/RenderPath3D.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_RENDERPATH(RENDERER, RET) RenderPath3D* RENDERER = (RenderPath3D*)canvas::GetCanvas(componentVID_); \
	if (!RENDERER) {post(type_ + "(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;} \
	if (RENDERER->GetType() != "RenderPath3D") {post(type_ + "(" + to_string(componentVID_) + ") is NOT RenderPath3D! (" + RENDERER->GetType() + ")", LogLevel::Error); return RET;}

	void VzRenderer::SetCanvas(const uint32_t w, const uint32_t h, const float dpi, void* window)
	{
		GET_RENDERPATH(renderer, );
		renderer->SetCanvas(w, h, dpi, window);
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

	void VzRenderer::SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits)
	{
		// to do //
		// add visibleMask(uint8_t) to RenderPath3D
	}

	bool VzRenderer::Render(const VID vidScene, const VID vidCam)
	{
		GET_RENDERPATH(renderer, false);

		renderer->scene = Scene::GetScene(vidScene);
		renderer->camera = compfactory::GetCameraComponent(vidCam);

		// todo DeltaTime...
		renderer->Update(0.f);
		renderer->Render();

		return true;
	}
}