#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

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
		hierarchy_child->SetParent(0u);
	}
	void VzScene::AttachToParent(const VzBaseComp* parent)
	{
		vzm::AppendSceneCompTo(this, parent);
	}
}