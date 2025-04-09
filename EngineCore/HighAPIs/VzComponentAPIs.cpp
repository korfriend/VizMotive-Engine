#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_NAME_COMP(COMP, RET) NameComponent* COMP = compfactory::GetNameComponent(componentVID_); \
	if (!COMP) {post("NameComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}
	
	std::string VzBaseComp::GetName() const 
	{
		GET_NAME_COMP(comp, "");
		return comp->GetName();
	}
	void VzBaseComp::SetName(const std::string& name)
	{
		GET_NAME_COMP(comp, );
		comp->SetName(name);
		UpdateTimeStamp();
	}
}

namespace vzm
{
#define GET_TRANS_COMP(COMP, RET) TransformComponent* COMP = compfactory::GetTransformComponent(componentVID_); \
	if (!COMP) {post("TransformComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	bool VzSceneComp::IsDirtyTransform() const
	{
		GET_TRANS_COMP(transform, true);
		return transform->IsDirty();
	}

	bool VzSceneComp::IsMatrixAutoUpdate() const
	{
		GET_TRANS_COMP(transform, false);
		return transform->IsMatrixAutoUpdate();
	}

	void VzSceneComp::SetMatrixAutoUpdate(const bool enable)
	{
		GET_TRANS_COMP(transform, );
		transform->SetMatrixAutoUpdate(enable);
		UpdateTimeStamp();
	}

	void VzSceneComp::GetWorldPosition(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldPosition();
	}
	void VzSceneComp::GetWorldRotation(vfloat4& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT4*)&v = transform->GetWorldRotation();
	}
	void VzSceneComp::GetWorldScale(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldScale();
	}
	void VzSceneComp::GetWorldForward(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldForward();
	}
	void VzSceneComp::GetWorldRight(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldRight();
	}
	void VzSceneComp::GetWorldUp(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldUp();
	}
	void VzSceneComp::GetWorldMatrix(vfloat4x4& mat, const bool rowMajor) const
	{
		GET_TRANS_COMP(transform, );
		const XMFLOAT4X4& world = transform->GetWorldMatrix();
		if (rowMajor)
		{
			*(XMFLOAT4X4*)&mat = world;
		}
		else
		{
			XMFLOAT4X4 world_col_maj;
			//XMMATRIX worldx_t = XMMatrixTranspose(XMLoadFloat4x4(&world));
			//XMStoreFloat4x4(&world_col_maj, worldx_t);
			XMStoreFloat4x4(&world_col_maj, XMMatrixTranspose(XMLoadFloat4x4(&world)));
			*(XMFLOAT4X4*)&mat = world_col_maj;
		}
	}

	void VzSceneComp::GetPosition(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetPosition();
	}
	void VzSceneComp::GetRotation(vfloat4& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT4*)&v = transform->GetRotation();
	}
	void VzSceneComp::GetScale(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetScale();
	}
	void VzSceneComp::GetLocalMatrix(vfloat4x4& mat, const bool rowMajor) const
	{
		GET_TRANS_COMP(transform, );
		const XMFLOAT4X4& local = transform->GetLocalMatrix();
		if (rowMajor)
		{
			*(XMFLOAT4X4*)&mat = local;
		}
		else
		{
			XMFLOAT4X4 local_col_maj;
			XMStoreFloat4x4(&local_col_maj, XMMatrixTranspose(XMLoadFloat4x4(&local)));
			*(XMFLOAT4X4*)&mat = local_col_maj;
		}
	}

	// local transforms
	void VzSceneComp::SetPosition(const vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetPosition(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzSceneComp::SetScale(const vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetScale(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzSceneComp::SetEulerAngleZXY(const vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetEulerAngleZXY(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzSceneComp::SetEulerAngleZXYInDegree(const vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetEulerAngleZXYInDegree(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzSceneComp::SetQuaternion(const vfloat4& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetQuaternion(*(XMFLOAT4*)&v);
		UpdateTimeStamp();
	}
	void VzSceneComp::SetRotateAxis(const vfloat3& v, const float angle)
	{
		GET_TRANS_COMP(transform, );
		transform->SetRotateAxis(*(XMFLOAT3*)&v, angle);
		UpdateTimeStamp();
	}
	void VzSceneComp::SetMatrix(const vfloat4x4& mat, const bool rowMajor)
	{
		GET_TRANS_COMP(transform, );
		XMFLOAT4X4 mat_in = *(XMFLOAT4X4*)&mat;
		if (!rowMajor)
		{
			XMStoreFloat4x4(&mat_in, XMMatrixTranspose(XMLoadFloat4x4(&mat_in)));
		}
		transform->SetMatrix(mat_in);
		UpdateTimeStamp();
	}

	void VzSceneComp::UpdateMatrix()
	{
		GET_TRANS_COMP(transform, );
		transform->UpdateMatrix();
		UpdateTimeStamp();
	}

	void VzSceneComp::UpdateWorldMatrix()
	{
		GET_TRANS_COMP(transform, );
		transform->UpdateWorldMatrix();
		UpdateTimeStamp();
	}

#define GET_HIER_COMP(COMP, RET) HierarchyComponent* COMP = compfactory::GetHierarchyComponent(componentVID_); \
	if (!COMP) {post("HierarchyComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	VID VzSceneComp::GetParent() const
	{
		GET_HIER_COMP(hierarchy, INVALID_VID);
		return compfactory::GetEntityByVUID(hierarchy->GetParent());
	}

	std::vector<VID> VzSceneComp::GetChildren() const
	{
		std::vector<VID> children;
		GET_HIER_COMP(hierarchy, children);
		std::vector<VUID> vuids = hierarchy->GetChildren();
		children.reserve(vuids.size());
		for (auto it : vuids)
		{
			children.push_back(compfactory::GetEntityByVUID(it));
		}
		return children;
	}

	void VzSceneComp::AppendChild(const VzBaseComp* child)
	{
		vzm::AppendSceneCompTo(child, this);
	}
	void VzSceneComp::DetachChild(const VzBaseComp* child)
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
		hierarchy_child->SetParent(INVALID_ENTITY);
	}
	void VzSceneComp::AttachToParent(const VzBaseComp* parent)
	{
		vzm::AppendSceneCompTo(this, parent);
	}
}
