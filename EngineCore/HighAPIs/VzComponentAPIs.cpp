#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Common/Engine_Internal.h"
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

	bool VzSceneObject::IsDirtyTransform() const
	{
		GET_TRANS_COMP(transform, true);
		return transform->IsDirty();
	}

	bool VzSceneObject::IsMatrixAutoUpdate() const
	{
		GET_TRANS_COMP(transform, false);
		return transform->IsMatrixAutoUpdate();
	}

	void VzSceneObject::SetMatrixAutoUpdate(const bool enable)
	{
		GET_TRANS_COMP(transform, );
		transform->SetMatrixAutoUpdate(enable);
		UpdateTimeStamp();
	}

	void VzSceneObject::GetWorldPosition(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldPosition();
	}
	void VzSceneObject::GetWorldRotation(vfloat4& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT4*)&v = transform->GetWorldRotation();
	}
	void VzSceneObject::GetWorldScale(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldScale();
	}
	void VzSceneObject::GetWorldForward(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldForward();
	}
	void VzSceneObject::GetWorldRight(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldRight();
	}
	void VzSceneObject::GetWorldUp(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldUp();
	}
	void VzSceneObject::GetWorldMatrix(vfloat4x4& mat, const bool rowMajor) const
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

	void VzSceneObject::GetPosition(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetPosition();
	}
	void VzSceneObject::GetRotation(vfloat4& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT4*)&v = transform->GetRotation();
	}
	void VzSceneObject::GetScale(vfloat3& v) const
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetScale();
	}
	void VzSceneObject::GetLocalMatrix(vfloat4x4& mat, const bool rowMajor) const
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
	void VzSceneObject::SetPosition(const vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetPosition(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzSceneObject::SetScale(const vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetScale(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzSceneObject::SetEulerAngleZXY(const vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetEulerAngleZXY(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzSceneObject::SetEulerAngleZXYInDegree(const vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetEulerAngleZXYInDegree(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzSceneObject::SetQuaternion(const vfloat4& v)
	{
		GET_TRANS_COMP(transform, );
		transform->SetQuaternion(*(XMFLOAT4*)&v);
		UpdateTimeStamp();
	}
	void VzSceneObject::SetRotateAxis(const vfloat3& v, const float angle)
	{
		GET_TRANS_COMP(transform, );
		transform->SetRotateAxis(*(XMFLOAT3*)&v, angle);
		UpdateTimeStamp();
	}
	void VzSceneObject::SetRotateToLookUp(const vfloat3& view, const vfloat3& up)
	{
		GET_TRANS_COMP(transform, );

		XMVECTOR look_to = XMLoadFloat3((XMFLOAT3*)&view);
		look_to = XMVector3Normalize(look_to);
		XMVECTOR look_up = XMLoadFloat3((XMFLOAT3*)&up);
		XMVECTOR right = XMVector3Cross(look_to, look_up);
		vzlog_assert(XMVectorGetX(XMVector3LengthSq(right)) > 0.0001f, "view and up must be different vector!");
		right = XMVector3Normalize(right);
		look_up = XMVector3Cross(right, look_to);
		XMMATRIX mat_r = VZMatrixLookTo(XMVectorSet(0, 0, 0, 1.f), look_to, look_up);
		XMVECTOR q_view = XMQuaternionRotationMatrix(mat_r);
		XMVECTOR q = XMQuaternionConjugate(q_view); // use this instead of using XMQuaternionInverse due to unit q_view by XMQuaternionRotationMatrix 
		XMFLOAT4 qt;
		XMStoreFloat4(&qt, q);
		transform->SetQuaternion(qt);
		UpdateTimeStamp();
	}
	void VzSceneObject::SetMatrix(const vfloat4x4& mat, const bool rowMajor)
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

	void VzSceneObject::UpdateMatrix()
	{
		GET_TRANS_COMP(transform, );
		transform->UpdateMatrix();
		UpdateTimeStamp();
	}

	void VzSceneObject::UpdateWorldMatrix()
	{
		GET_TRANS_COMP(transform, );
		transform->UpdateWorldMatrix();
		UpdateTimeStamp();
	}

#define GET_HIER_COMP(COMP, RET) HierarchyComponent* COMP = compfactory::GetHierarchyComponent(componentVID_); \
	if (!COMP) {post("HierarchyComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	VID VzSceneObject::GetParent() const
	{
		GET_HIER_COMP(hierarchy, INVALID_VID);
		return compfactory::GetEntityByVUID(hierarchy->GetParent());
	}

	std::vector<VID> VzSceneObject::GetChildren() const
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

	void VzSceneObject::AppendChild(const VzBaseComp* child)
	{
		vzm::AppendSceneCompTo(child, this);
	}
	void VzSceneObject::DetachChild(const VzBaseComp* child)
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
	void VzSceneObject::AttachToParent(const VzBaseComp* parent)
	{
		vzm::AppendSceneCompTo(this, parent);
	}

#define GET_LAYEREDMASK_COMP(COMP, RET) LayeredMaskComponent* COMP = compfactory::GetLayeredMaskComponent(componentVID_); \
	if (!COMP) {post("LayeredMaskComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	// Visible Layer Settings
	void VzSceneObject::SetVisibleLayerMask(const uint32_t visibleLayerMask, const bool includeDescendants)
	{
		GET_LAYEREDMASK_COMP(layeredmask, );
		layeredmask->SetVisibleLayerMask(visibleLayerMask);

		if (includeDescendants)
		{
			std::vector<ActorVID> children = GetChildren();
			for (size_t i = 0, n = children.size(); i < n; ++i)
			{
				VzSceneObject* scene_comp = (VzSceneObject*)vzm::GetComponent(children[i]);
				assert(scene_comp);
				scene_comp->SetVisibleLayerMask(visibleLayerMask, true);
			}
		}
		UpdateTimeStamp();
	}
	uint32_t VzSceneObject::GetVisibleLayerMask() const
	{
		GET_LAYEREDMASK_COMP(layeredmask, 0u);
		return layeredmask->GetVisibleLayerMask();
	}
	bool VzSceneObject::IsVisibleWith(const uint32_t layerBits) const
	{
		GET_LAYEREDMASK_COMP(layeredmask, false);
		return layeredmask->IsVisibleWith(layerBits);
	}

	void VzSceneObject::SetUserLayerMask(const uint32_t userLayerMask, const bool includeDescendants)
	{
		GET_LAYEREDMASK_COMP(layeredmask, );
		layeredmask->SetUserLayerMask(userLayerMask);

		if (includeDescendants)
		{
			std::vector<ActorVID> children = GetChildren();
			for (size_t i = 0, n = children.size(); i < n; ++i)
			{
				VzSceneObject* scene_comp = (VzSceneObject*)vzm::GetComponent(children[i]);
				assert(scene_comp);
				scene_comp->SetUserLayerMask(userLayerMask, true);
			}
		}
		UpdateTimeStamp();
	}
	uint32_t VzSceneObject::GetUserLayerMask() const
	{
		GET_LAYEREDMASK_COMP(layeredmask, 0u);
		return layeredmask->GetUserLayerMask();
	}
}

namespace vzm
{
#define GET_LAYEREDMASK_COMP_OR_NEW(COMP) LayeredMaskComponent* COMP = compfactory::GetLayeredMaskComponent(componentVID_); \
	if (!COMP) { COMP = compfactory::CreateLayeredMaskComponent(componentVID_); }
	
	void VzResource::SetVisibleLayerMask(const uint32_t visibleLayerMask)
	{
		GET_LAYEREDMASK_COMP_OR_NEW(layeredmask);
		layeredmask->SetVisibleLayerMask(visibleLayerMask);
		UpdateTimeStamp();
	}
}