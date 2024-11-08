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
	
	std::string VzBaseComp::GetName()
	{
		GET_NAME_COMP(comp, "");
		return comp->name;
	}
	void VzBaseComp::SetName(const std::string& name)
	{
		GET_NAME_COMP(comp, );
		comp->name = name;
		UpdateTimeStamp();
	}
}

namespace vzm
{
#define GET_TRANS_COMP(COMP, RET) TransformComponent* COMP = compfactory::GetTransformComponent(componentVID_); \
	if (!COMP) {post("TransformComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	bool VzSceneComp::IsDirtyTransform()
	{
		GET_TRANS_COMP(transform, true);
		return transform->IsDirty();
	}

	bool VzSceneComp::IsMatrixAutoUpdate()
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

	void VzSceneComp::GetWorldPosition(vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldPosition();
	}
	void VzSceneComp::GetWorldRotation(vfloat4& v)
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT4*)&v = transform->GetWorldRotation();
	}
	void VzSceneComp::GetWorldScale(vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldScale();
	}
	void VzSceneComp::GetWorldForward(vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldForward();
	}
	void VzSceneComp::GetWorldRight(vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldRight();
	}
	void VzSceneComp::GetWorldUp(vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetWorldUp();
	}
	void VzSceneComp::GetWorldMatrix(vfloat4x4& mat, const bool rowMajor)
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

	void VzSceneComp::GetPosition(vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetPosition();
	}
	void VzSceneComp::GetRotation(vfloat4& v)
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT4*)&v = transform->GetRotation();
	}
	void VzSceneComp::GetScale(vfloat3& v)
	{
		GET_TRANS_COMP(transform, );
		*(XMFLOAT3*)&v = transform->GetScale();
	}
	void VzSceneComp::GetLocalMatrix(vfloat4x4& mat, const bool rowMajor)
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

	VID VzSceneComp::GetParent()
	{
		GET_HIER_COMP(hierarchy, INVALID_VID);
		return compfactory::GetEntityByVUID(hierarchy->GetParent());
	}

	std::vector<VID> VzSceneComp::GetChildren()
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
}

//#include "Utils/Jobsystem.h"
namespace vzm
{
#define GET_CAM_COMP(COMP, RET) CameraComponent* COMP = compfactory::GetCameraComponent(componentVID_); \
	if (!COMP) {post("CameraComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzCamera::SetWorldPoseByHierarchy()
	{
		GET_CAM_COMP(camera, );
		camera->SetWorldLookAtFromHierarchyTransforms();
		camera->UpdateMatrix();
		UpdateTimeStamp();
	}
	void VzCamera::SetWorldPose(const vfloat3& pos, const vfloat3& view, const vfloat3& up)
	{
		//jobsystem::context ctx;
		//jobsystem::Execute(ctx, [&](jobsystem::JobArgs args) {
			GET_CAM_COMP(camera, );
			camera->SetWorldLookTo(*(XMFLOAT3*)&pos, *(XMFLOAT3*)&view, *(XMFLOAT3*)&up);
			camera->UpdateMatrix();
			UpdateTimeStamp();
		//	});
		//jobsystem::Wait(ctx);
	}
	void VzCamera::SetPerspectiveProjection(const float zNearP, const float zFarP, const float fovInDegree, const float aspectRatio, const bool isVertical)
	{
		GET_CAM_COMP(camera, );
		camera->SetPerspective(aspectRatio, 1.f, zNearP, zFarP, XMConvertToRadians(isVertical ? fovInDegree : fovInDegree / aspectRatio));
		camera->UpdateMatrix();
		UpdateTimeStamp();
	}
	void VzCamera::GetWorldPose(vfloat3& pos, vfloat3& view, vfloat3& up)
	{
		GET_CAM_COMP(camera, );
		if (camera->IsDirty())
		{
			camera->UpdateMatrix();
		}
		XMFLOAT3 _eye = camera->GetWorldEye();
		XMFLOAT3 _at = camera->GetWorldAt();
		XMFLOAT3 _up = camera->GetWorldUp();
		XMFLOAT3 _view;
		XMStoreFloat3(&_view, XMLoadFloat3(&_at) - XMLoadFloat3(&_eye));
		*(XMFLOAT3*)&pos = _eye;
		*(XMFLOAT3*)&view = _view;
		*(XMFLOAT3*)&up = _up;
	}
	void VzCamera::GetPerspectiveProjection(float* zNearP, float* zFarP, float* fovInDegree, float* aspectRatio, bool isVertical)
	{
		GET_CAM_COMP(camera, );
		if (camera->GetProjectionType() != CameraComponent::Projection::PERSPECTIVE)
		{
			return;
		}
		float aspect, near_p, far_p;
		camera->GetWidthHeight(&aspect, nullptr);
		camera->GetNearFar(&near_p, &far_p);
		if (fovInDegree)
		{
			*fovInDegree = XMConvertToDegrees(isVertical ? camera->GetFovVertical() : camera->GetFovVertical() / aspect);
		}
		if (aspectRatio)
		{
			*aspectRatio = aspect;
		}
		if (zNearP)
		{
			*zNearP = near_p;
		}
		if (zFarP)
		{
			*zFarP = far_p;
		}
	}

	float VzCamera::GetNear()
	{
		GET_CAM_COMP(camera, -1.f);
		float ret;
		camera->GetNearFar(&ret, nullptr);
		return ret;
	}
	float VzCamera::GetCullingFar()
	{
		GET_CAM_COMP(camera, -1.f);
		float ret;
		camera->GetNearFar(nullptr, &ret);
		return ret;
	}
}