#include "Components.h"
#include "Utils/Backlog.h"

namespace vz
{
	XMFLOAT3 TransformComponent::GetWorldPosition() const
	{
		return *((XMFLOAT3*)&world_._41);
	}
	XMFLOAT4 TransformComponent::GetWorldRotation() const
	{
		XMVECTOR S, R, T;
		XMMatrixDecompose(&S, &R, &T, XMLoadFloat4x4(&world_));
		XMFLOAT4 rotation;
		XMStoreFloat4(&rotation, R);
		return rotation;
	}
	XMFLOAT3 TransformComponent::GetWorldScale() const
	{
		XMVECTOR S, R, T;
		XMMatrixDecompose(&S, &R, &T, XMLoadFloat4x4(&world_));
		XMFLOAT3 scale;
		XMStoreFloat3(&scale, S);
		return scale;
	}
	XMFLOAT3 TransformComponent::GetWorldForward() const
	{
		return vz::math::GetForward(world_);
	}
	XMFLOAT3 TransformComponent::GetWorldUp() const
	{
		return vz::math::GetUp(world_);
	}
	XMFLOAT3 TransformComponent::GetWorldRight() const
	{
		return vz::math::GetRight(world_);
	}
	void TransformComponent::SetEulerAngleZXY(const XMFLOAT3& rotAngles)
	{
		isDirty_ = true;

		// This needs to be handled a bit differently
		
		XMVECTOR quat = XMLoadFloat4(&rotation_);
		XMVECTOR x = XMQuaternionRotationRollPitchYaw(rotAngles.x, 0, 0);
		XMVECTOR y = XMQuaternionRotationRollPitchYaw(0, rotAngles.y, 0);
		XMVECTOR z = XMQuaternionRotationRollPitchYaw(0, 0, rotAngles.z);

		quat = XMQuaternionMultiply(x, quat);
		quat = XMQuaternionMultiply(quat, y);
		quat = XMQuaternionMultiply(z, quat);
		quat = XMQuaternionNormalize(quat);

		XMStoreFloat4(&rotation_, quat);
	}
	void TransformComponent::SetEulerAngleZXYInDegree(const XMFLOAT3& rotAngles)
	{
		SetEulerAngleZXY(XMFLOAT3(
			XMConvertToRadians(rotAngles.x), XMConvertToRadians(rotAngles.y), XMConvertToRadians(rotAngles.z)
		));
	}

	void TransformComponent::SetMatrix(const XMFLOAT4X4& local)
	{
		if (isMatrixAutoUpdate_)
		{
			// decompose
			XMVECTOR S, R, T;
			XMMatrixDecompose(&S, &R, &T, XMLoadFloat4x4(&local));
			XMStoreFloat3(&scale_, S);
			XMStoreFloat4(&rotation_, R);
			XMStoreFloat3(&position_, T);
		}
		else
		{
			local_ = local;
		}
		isDirty_ = false;
	}

	void TransformComponent::UpdateMatrix()
	{
		if (!isMatrixAutoUpdate_)
		{
			// use local_
			return;
		}
		XMVECTOR S_local = XMLoadFloat3(&scale_);
		XMVECTOR R_local = XMLoadFloat4(&rotation_);
		XMVECTOR T_local = XMLoadFloat3(&position_);
		XMStoreFloat4x4(&local_, 
			XMMatrixScalingFromVector(S_local) *
			XMMatrixRotationQuaternion(R_local) *
			XMMatrixTranslationFromVector(T_local));

		isDirty_ = false;
	}
}

namespace vz
{
	void RenderableComponent::SetGeometry(const Entity geometryEntity)
	{
		GeometryComponent* geo_comp = compfactory::GetGeometryComponent(geometryEntity);
		if (geo_comp == nullptr)
		{
			backlog::post("invalid entity", backlog::LogLevel::Error);
			return;
		}
		geometryEntity_ = geometryEntity;
		isValid_ = geo_comp->GetNumParts() == materialEntities_.size();
	}
	bool RenderableComponent::SetMaterial(const Entity materialEntity, const size_t slot)
	{
		if (slot >= materialEntities_.size())
		{
			backlog::post("slot is over current materials", backlog::LogLevel::Error);
			return false;
		}
		MaterialComponent* mat_comp = compfactory::GetMaterialComponent(materialEntity);
		if (mat_comp == nullptr)
		{
			backlog::post("invalid entity", backlog::LogLevel::Error);
			return false;
		}
		materialEntities_[slot] = materialEntity;
		return true;
	}
	void RenderableComponent::SetMaterials(const std::vector<Entity>& materials)
	{
		materialEntities_ = materials;
		isValid_ = false;
		GeometryComponent* geo_comp = compfactory::GetGeometryComponent(geometryEntity_);
		if (geo_comp)
		{
			isValid_ = geo_comp->GetNumParts() == materialEntities_.size();
		}
	}
}

namespace vz
{
	void CameraComponent::SetPerspective(float width, float height, float nearP, float farP, float fovY)
	{
		width_ = width;
		height_ = height;
		zNearP_ = nearP;
		zFarP_ = farP;
		fovY_ = fovY;
		timeStampSetter_ = TimerNow;
		//isDirty_ = true;

		UpdateMatrix();
	}

	void CameraComponent::UpdateMatrix()
	{
		if (projectionType_ != enums::Projection::CUSTOM_PROJECTION)
		{
			XMStoreFloat4x4(&projection_, VZMatrixPerspectiveFov(fovY_, width_ / height_, zFarP_, zNearP_)); // reverse zbuffer!
			projection_.m[2][0] = jitter.x;
			projection_.m[2][1] = jitter.y;
		}

		XMVECTOR _Eye = XMLoadFloat3(&eye_);
		XMVECTOR _At = XMLoadFloat3(&at_);
		XMVECTOR _Up = XMLoadFloat3(&up_);

		XMMATRIX _V = VZMatrixLookTo(_Eye, _At, _Up);
		XMStoreFloat4x4(&view_, _V);

		XMMATRIX _P = XMLoadFloat4x4(&projection_);
		XMMATRIX _InvP = XMMatrixInverse(nullptr, _P);
		XMStoreFloat4x4(&invProjection_, _InvP);

		XMMATRIX _VP = XMMatrixMultiply(_V, _P);
		XMStoreFloat4x4(&view_, _V);
		XMStoreFloat4x4(&viewProjection_, _VP);
		XMMATRIX _InvV = XMMatrixInverse(nullptr, _V);
		XMStoreFloat4x4(&invView_, _InvV);
		XMStoreFloat3x3(&rotationMatrix_, _InvV);
		XMStoreFloat4x4(&invViewProjection_, XMMatrixInverse(nullptr, _VP));

		frustum_.Create(_VP);

		isDirty_ = false;
	}
}