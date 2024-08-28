#include "Components.h"

namespace vz::component
{
	using namespace vz;

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

	}
	XMFLOAT3 TransformComponent::GetWorldUp() const
	{

	}
	XMFLOAT3 TransformComponent::GetWorldRight() const
	{

	}
	void TransformComponent::SetRotation(const XMFLOAT3& rotAngles, const enums::EulerAngle euler)
	{
		
	}
	void TransformComponent::UpdateMatrix()
	{

	}
	void TransformComponent::ApplyTransform()
	{

	}
}