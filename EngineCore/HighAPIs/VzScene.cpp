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
		hierarchy_child->SetParentByVUID(0ull);
	}
	void VzScene::AttachToParent(const VzBaseComp* parent)
	{
		vzm::AppendSceneCompTo(this, parent);
	}

	const std::vector<VID>& VzScene::GetChildrenVIDs() const
	{
		Scene* scene = scenefactory::GetScene(this->componentVID_);
		return scene->GetChildrenEntities();
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

		Scene* scene = scenefactory::GetScene(this->componentVID_);
	
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

	bool VzScene::LoadIBL(const std::string& iblPath)
	{
		Scene* scene = scenefactory::GetScene(this->componentVID_);
		assert(scene);
		EnvironmentComponent* env = compfactory::GetEnvironmentComponent(scene->GetEnvironment());
		if (env == nullptr)
		{
			vzlog_error("VzScene(%s) has no EnvironmentComponent!", scene->GetSceneName().c_str());
			return false;
		}
		return env->LoadSkyMap(iblPath);
	}
}