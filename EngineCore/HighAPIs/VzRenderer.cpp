#include "VzEngineAPIs.h"
#include "Components/GComponents.h"
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

	void VzRenderer::ResizeCanvas(const uint32_t w, const uint32_t h, const CamVID vidCam)
	{
		GET_RENDERPATH(renderer, );

		uint32_t w1 = std::max(w, 1u);
		uint32_t h1 = std::max(h, 1u);
		renderer->SetCanvas(w1, h1, renderer->GetDPI(), renderer->GetWindow());

		auto resizeCamera = [](CameraComponent* camera, const uint32_t w, const uint32_t h)
			{
				float z_near, z_far;
				camera->GetNearFar(&z_near, &z_far);
				camera->SetPerspective(
					(float)w / (float)h,
					1.f, z_near, z_far, camera->GetFovVertical()
				);
			};

		if (renderer->camera)
		{
			resizeCamera(renderer->camera, w1, h1);
		}

		CameraComponent* user_camera = compfactory::GetCameraComponent(vidCam);
		if (user_camera && user_camera != renderer->camera)
		{
			resizeCamera(user_camera, w1, h1);
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

		renderer->useManualSetViewport = true;

		graphics::Viewport vp;
		vp.top_left_x = x;
		vp.top_left_y = y;
		vp.width = w;
		vp.height = h;
		renderer->SetViewport(vp);
	}

	void VzRenderer::GetViewport(float* VZ_NULLABLE x, float* VZ_NULLABLE y, float* VZ_NULLABLE w, float* VZ_NULLABLE h)
	{
		GET_RENDERPATH(renderer, );
		const graphics::Viewport& vp = renderer->GetViewport();
		if (x) *x = vp.top_left_x;
		if (y) *y = vp.top_left_y;
		if (w) *w = vp.width;
		if (h) *h = vp.height;
	}

	void VzRenderer::UseCanvasAsViewport()
	{
		GET_RENDERPATH(renderer, );

		renderer->useManualSetViewport = false;

		graphics::Viewport vp;
		vp.top_left_x = 0;
		vp.top_left_y = 0;
		vp.width = (float)renderer->GetPhysicalWidth();
		vp.height = (float)renderer->GetPhysicalHeight();
		renderer->SetViewport(vp);
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

	void VzRenderer::SetTonemap(const Tonemap tonemap)
	{
		GET_RENDERPATH(renderer, );
	}

	bool VzRenderer::Render(const SceneVID vidScene, const CamVID vidCam)
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

	bool VzRenderer::PickingList(const SceneVID vidScene, const CamVID vidCam, const vfloat2& pos, 
		std::vector<vfloat3>& worldPositions, std::vector<ActorVID>& vids,
		std::vector<int>& pritmitiveIDs, std::vector<int>& maskValues)
	{
		GET_RENDERPATH(renderer, false);

		GCameraComponent* camera = (GCameraComponent*)compfactory::GetCameraComponent(vidCam);
		if (camera == nullptr)
		{
			return false;
		}

		renderer->scene = Scene::GetScene(vidScene);
		renderer->camera = compfactory::GetCameraComponent(vidCam);

		if (!renderer->scene || !renderer->camera)
		{
			return false;
		}

		camera->isPickingMode = true;
		camera->pickingIO.SetScreenPos(*(XMFLOAT2*)&pos);

		renderer->Render(0); // just for picking process

		size_t num_picked_positions = camera->pickingIO.NumPickedPositions();
		bool ret = num_picked_positions > 0;

		// output setting
		if (ret)
		{
			vids.resize(num_picked_positions);
			worldPositions.resize(num_picked_positions);
			pritmitiveIDs.resize(num_picked_positions);
			maskValues.resize(num_picked_positions);
			memcpy(vids.data(), camera->pickingIO.DataEntities(), sizeof(Entity) * num_picked_positions);
			memcpy(worldPositions.data(), camera->pickingIO.DataPositions(), sizeof(XMFLOAT3) * num_picked_positions);
			memcpy(pritmitiveIDs.data(), camera->pickingIO.DataPrimitiveIDs(), sizeof(int) * num_picked_positions);
			memcpy(maskValues.data(), camera->pickingIO.DataMaskValues(), sizeof(int) * num_picked_positions);
		}

		camera->isPickingMode = false;
		camera->pickingIO.Clear();

		return ret;
	}

	vfloat3 VzRenderer::UnprojToWorld(const vfloat2& posOnScreen, const VzCamera* camera)
	{
		GET_RENDERPATH(renderer, {});

		CameraComponent* cam_component = nullptr;
		if (camera != nullptr)
		{
			cam_component = compfactory::GetCameraComponent(camera->GetVID());
		}
		else
		{
			cam_component = renderer->camera;
		}

		if (cam_component == nullptr)
		{
			backlog::post("VzRenderer::Unproj >> Invalid camera!", backlog::LogLevel::Error);
			return {};
		}

		const XMFLOAT4X4& inv_VP = cam_component->GetInvViewProjection();
		XMFLOAT4X4 inv_screen;
		renderer->GetViewportTransforms(nullptr, &inv_screen);

		XMMATRIX xinv_screen = XMLoadFloat4x4(&inv_screen);
		XMMATRIX xinv_VP = XMLoadFloat4x4(&inv_VP);
		XMMATRIX xmat_screen2world = xinv_screen * xinv_VP;

		XMFLOAT3 pos_ss3 = XMFLOAT3(posOnScreen.x, posOnScreen.y, 1.f); // consider reverse z
		XMVECTOR xpos_ss = XMLoadFloat3(&pos_ss3);
		XMVECTOR xpos_ws = XMVector3TransformCoord(xpos_ss, xmat_screen2world);
		vfloat3 pos_ws;
		XMStoreFloat3((XMFLOAT3*)&pos_ws, xpos_ws);
		return pos_ws;
	}

	bool VzRenderer::GetSharedRenderTarget(const void* graphicsDev2, const void* srvDescHeap2, const int descriptorIndex, SharedResourceTarget& resTarget, uint32_t* w, uint32_t* h)
	{
		GET_RENDERPATH(renderer, false);

		if (w) *w = renderer->GetPhysicalWidth();
		if (h) *h = renderer->GetPhysicalHeight();

		//if (graphicsDevice == nullptr) return nullptr;
		//return graphicsDevice->OpenSharedResource(graphicsDev2, const_cast<wi::graphics::Texture*>(&renderer->GetRenderResult()));
		//return graphicsDevice->OpenSharedResource(graphicsDev2, &renderer->rtPostprocess);

		resTarget = {};
		bool ret = renderer->GetSharedRendertargetView(graphicsDev2, srvDescHeap2, descriptorIndex, resTarget.descriptorHandle, &resTarget.resourcePtr);

		return ret;
	}

	void VzRenderer::ShowDebugBuffer(const std::string& debugMode)
	{
		GET_RENDERPATH(renderer, );
		renderer->ShowDebugBuffer(debugMode);
	}
}