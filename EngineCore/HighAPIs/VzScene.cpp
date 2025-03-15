#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Common/RenderPath3D.h"
#include "Common/Engine_Internal.h"
#include "Utils/Utils_Internal.h"
#include "Utils/Backlog.h"
#include "Utils/Profiler.h"
#include "Utils/Helpers.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
	void VzScene::SetVisibleLayerMask(const uint32_t visibleLayerMask)
	{
		Scene* scene = Scene::GetScene(this->componentVID_);
		scene->SetVisibleLayerMask(visibleLayerMask);
		UpdateTimeStamp();
	}
	void VzScene::SetVisibleLayer(const bool visible, const uint32_t layerBits)
	{
		Scene* scene = Scene::GetScene(this->componentVID_);
		scene->SetVisibleLayer(visible, layerBits);
		UpdateTimeStamp();
	}
	uint32_t VzScene::GetVisibleLayerMask() const
	{
		Scene* scene = Scene::GetScene(this->componentVID_);
		return scene->GetVisibleLayerMask();
	}

	void VzScene::AppendChild(const VzBaseComp* child)
	{
		vzm::AppendSceneCompTo(child, this);
	}
	void VzScene::DetachChild(const VzBaseComp* child)
	{
		if (child == nullptr)
		{
			return;
		}
		HierarchyComponent* hierarchy_child = compfactory::GetHierarchyComponent(child->GetVID());
		if (hierarchy_child == nullptr)
		{
			return;
		}
		if (hierarchy_child->GetParentEntity() != componentVID_)
		{
			return;
		}
		hierarchy_child->SetParent(0u);
	}
	void VzScene::AttachToParent(const VzBaseComp* parent)
	{
		vzm::AppendSceneCompTo(this, parent);
	}

	ChainUnitRCam::ChainUnitRCam(const RendererVID rendererVid, const CamVID camVid) : rendererVid(rendererVid), camVid(camVid)
	{
		VzBaseComp* renderer = vzm::GetComponent(rendererVid);
		VzBaseComp* camera = vzm::GetComponent(camVid);
		bool is_valid = renderer && camera;
		vzlog_assert(is_valid, "ChainUnitRCam >> Invalid VzComponent!!");
		if (!is_valid)
			return;
		is_valid = renderer->GetType() == COMPONENT_TYPE::RENDERER &&
			(camera->GetType() == COMPONENT_TYPE::CAMERA || camera->GetType() == COMPONENT_TYPE::SLICER);
		vzlog_assert(is_valid,
			"ChainUnitRCam >> Component Type Matching Error!!");
		if (!is_valid)
			return;
		isValid_ = true;
	}

	bool VzScene::RenderChain(const std::vector<ChainUnitRCam>& renderChain, const float dt)
	{
		if (renderChain.size() == 0)
			return false;

		std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());

		Scene* scene = Scene::GetScene(this->componentVID_);
	
		auto timestamp_prev = scene->timerStamp;
		float delta_time = float(std::max(0.0, scene->RecordElapsedSeconds()));
		const float target_deltaTime = 1.0f / scene->targetFrameRate;
		if (scene->framerateLock && delta_time < target_deltaTime)
		{
			if (scene->frameskip)
			{
				scene->timerStamp = timestamp_prev;
				return true;
			}
			helper::QuickSleep((target_deltaTime - delta_time) * 1000);
			delta_time += float(std::max(0.0, scene->RecordElapsedSeconds()));
		}

		profiler::BeginFrame();

		float update_dt = dt > 0 ? dt : delta_time;
		scene->Update(update_dt);

		for (const ChainUnitRCam& render_unit : renderChain)
		{
			if (!render_unit.IsValid())
			{
				vzlog_error("Invalid ChainUnitRCam");
				return false;
			}

			RenderPath3D* renderer = (RenderPath3D*)canvas::GetCanvas(render_unit.rendererVid);
			CameraComponent* camera = compfactory::GetCameraComponent(render_unit.camVid);

			renderer->scene = scene;
			renderer->camera = camera;

			renderer->deltaTimeAccumulator += delta_time;

			renderer->Update(update_dt);
			renderer->Render(update_dt);
			renderer->frameCount++;
		}

		if (!IsPendingSubmitCommand())
		{
			graphics::GraphicsDevice* device = graphics::GetDevice();
			graphics::CommandList cmd = device->BeginCommandList();
			profiler::EndFrame(&cmd); // cmd must be assigned before SubmitCommandLists
			device->SubmitCommandLists();
			vzm::ResetPendingSubmitCommand();
		}
		else
		{
			vzm::CountPendingSubmitCommand();
		}

		return true;
	}
}