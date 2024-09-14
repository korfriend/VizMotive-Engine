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
		setDirty(true);

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

	void TransformComponent::SetMatrix(XMFLOAT4X4 local)
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

	}
	void RenderableComponent::SetMaterials(const std::vector<Entity>& materials)
	{

	}
}