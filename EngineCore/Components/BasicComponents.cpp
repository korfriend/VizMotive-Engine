#include "GComponents.h"
#include "Utils/Backlog.h"

#include <cstdint>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>
#include <unordered_map>

namespace vz::uuid
{
	// 1. milliseconds about for 8925 years timestamp uniqueness
	// 2. randomness for additional entropy
	// 3. 256 uniques IDs are allowed for the same timestamp
	static std::atomic<uint16_t> sCounter;
	static std::mt19937_64 sRandomEngine;
	static std::once_flag randomInitFlag;
	uint64_t generateUUID(uint8_t cType) 
	{
		std::call_once(randomInitFlag, []() {
			std::random_device rd;
			sRandomEngine.seed(rd());
			});

		uint64_t uuid = 0;

		// High precision timestamp (48 bits)
		auto now = std::chrono::system_clock::now();
		auto duration = now.time_since_epoch();
		auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
		uint64_t timestamp = microseconds.count() & 0xFFFFFFFFFFFFull;
		uuid |= timestamp << 16;

		// Atomic counter (8 bits)
		uint16_t count = sCounter.fetch_add(1, std::memory_order_relaxed);
		uuid |= ((uint64_t)(count & 0xFF)) << 8;

		// Random component (8 bits)
		//uint8_t randomBits = static_cast<uint8_t>(sRandomEngine() & 0xFF);
		//uuid |= static_cast<uint64_t>(randomBits);
		assert(!(uuid & 0xFF));
		uuid |= cType;

		return uuid;
	}
}

namespace vz
{
	std::string GetComponentVersion()
	{
		return COMPONENT_INTERFACE_VERSION;
	}

	ComponentBase::ComponentBase(const ComponentType compType, const Entity entity, const VUID vuid) : cType_(compType), entity_(entity)
	{
		if (vuid == 0) vuid_ = uuid::generateUUID(static_cast<uint8_t>(compType));
		else vuid_ = vuid;
		timeStampSetter_ = TimerNow;
	}
}

namespace vz
{
	namespace compfactory
	{
		extern std::unordered_map<std::string, std::unordered_set<Entity>> lookupName2Entities;
	}

	void NameComponent::SetName(const std::string& name)
	{
		auto it = compfactory::lookupName2Entities.find(name_);
		if (it != compfactory::lookupName2Entities.end()) 
		{
			it->second.erase(entity_);
			if (it->second.size() == 0)
			{
				compfactory::lookupName2Entities.erase(name_);
			}
		}

		compfactory::lookupName2Entities[name].insert(entity_);
		name_ = name;
	}
}
namespace vz
{
	void HierarchyComponent::updateChildren() 
	{
		childrenCache_.clear(); 
		size_t n = children_.size();
		if (n > 0)
		{
			childrenCache_.reserve(children_.size());
			for (auto it : children_)
			{
				childrenCache_.push_back(it);
			}
		}
	}

	void HierarchyComponent::SetParentByVUID(const VUID vuidParent)
	{
		if (vuidParent == vuidParentHierarchy_)
		{
			return;
		}
		HierarchyComponent* old_parent_hierarchy = compfactory::GetHierarchyComponent(compfactory::GetEntityByVUID(vuidParentHierarchy_));
		if (old_parent_hierarchy)
		{
			old_parent_hierarchy->RemoveChild(vuid_);
		}
		timeStampSetter_ = TimerNow;
		if (vuidParent == 0u)
		{
			vuidParentHierarchy_ = 0u;
			return;
		}
		HierarchyComponent* parent_hierarchy = compfactory::GetHierarchyComponent(compfactory::GetEntityByVUID(vuidParent));
		assert(parent_hierarchy);
		vuidParentHierarchy_ = vuidParent;
		parent_hierarchy->AddChild(vuid_);

		compfactory::SetSceneComponentsDirty(entity_);
	}
	void HierarchyComponent::SetParent(const Entity entityParent)
	{
		HierarchyComponent* parent_hierarchy = compfactory::GetHierarchyComponent(entityParent);
		if (parent_hierarchy)
		{
			SetParentByVUID(parent_hierarchy->GetVUID());
		}
		else
		{
			SetParentByVUID(INVALID_VUID);
		}
	}
	VUID HierarchyComponent::GetParent() const
	{
		return vuidParentHierarchy_;
	}
	Entity HierarchyComponent::GetParentEntity() const
	{
		return compfactory::GetEntityByVUID(vuidParentHierarchy_);
	}

	void HierarchyComponent::AddChild(const VUID vuidChild)
	{
		children_.insert(vuidChild);
		updateChildren();
		timeStampSetter_ = TimerNow;
	}

	void HierarchyComponent::RemoveChild(const VUID vuidChild)
	{
		children_.erase(vuidChild);
		updateChildren();
		timeStampSetter_ = TimerNow;
	}
}

namespace vz
{
	inline float differenceMatrices(const XMFLOAT4X4& m0, const XMFLOAT4X4& m1)
	{
		const float* m0_ptr = (float*)&m0;
		const float* m1_ptr = (float*)&m1;
		float diff_sq = 0;
		for (size_t i = 0; i < 16; i++)
		{
			float f = m0_ptr[i] - m1_ptr[i];
			diff_sq += f * f;
		}
		return diff_sq;
	}

	const XMFLOAT3 TransformComponent::GetWorldPosition() const
	{
		return *((XMFLOAT3*)&world_._41);
	}
	const XMFLOAT4 TransformComponent::GetWorldRotation() const
	{
		XMVECTOR S, R, T;
		XMMatrixDecompose(&S, &R, &T, XMLoadFloat4x4(&world_));
		XMFLOAT4 rotation;
		XMStoreFloat4(&rotation, R);
		return rotation;
	}
	const XMFLOAT3 TransformComponent::GetWorldScale() const
	{
		XMVECTOR S, R, T;
		XMMatrixDecompose(&S, &R, &T, XMLoadFloat4x4(&world_));
		XMFLOAT3 scale;
		XMStoreFloat3(&scale, S);
		return scale;
	}
	const XMFLOAT3 TransformComponent::GetWorldForward() const
	{
		return vz::math::GetForward(world_);
	}
	const XMFLOAT3 TransformComponent::GetWorldUp() const
	{
		return vz::math::GetUp(world_);
	}
	const XMFLOAT3 TransformComponent::GetWorldRight() const
	{
		return vz::math::GetRight(world_);
	}
	void TransformComponent::SetEulerAngleZXY(const XMFLOAT3& rotAngles)
	{
		isDirty_ = true;

		// This needs to be handled a bit differently
		
		//XMVECTOR quat = XMLoadFloat4(&rotation_);
		XMVECTOR quat = XMQuaternionIdentity();
		//XMVECTOR x = XMQuaternionRotationRollPitchYaw(rotAngles.x, 0, 0);
		//XMVECTOR y = XMQuaternionRotationRollPitchYaw(0, rotAngles.y, 0);
		//XMVECTOR z = XMQuaternionRotationRollPitchYaw(0, 0, rotAngles.z);
		XMVECTOR qZ = XMQuaternionRotationRollPitchYaw(0, 0, rotAngles.z);
		XMVECTOR qX = XMQuaternionRotationRollPitchYaw(rotAngles.x, 0, 0);
		XMVECTOR qY = XMQuaternionRotationRollPitchYaw(0, rotAngles.y, 0);

		//quat = XMQuaternionMultiply(x, quat);
		//quat = XMQuaternionMultiply(quat, y);
		//quat = XMQuaternionMultiply(z, quat);
		//quat = XMQuaternionNormalize(quat);
		XMVECTOR finalQuat = XMQuaternionMultiply(qZ, XMQuaternionMultiply(qX, qY));
		finalQuat = XMQuaternionNormalize(finalQuat);
		//XMStoreFloat4(&rotation_, quat);
		XMStoreFloat4(&rotation_, finalQuat);

		timeStampSetter_ = TimerNow;
	}
	void TransformComponent::SetEulerAngleZXYInDegree(const XMFLOAT3& rotAngles)
	{
		SetEulerAngleZXY(XMFLOAT3(
			XMConvertToRadians(rotAngles.x), XMConvertToRadians(rotAngles.y), XMConvertToRadians(rotAngles.z)
		));
	}
	void TransformComponent::SetRotateAxis(const XMFLOAT3& axis, const float rotAngle)
	{
		XMVECTOR axisx = XMLoadFloat3(&axis);  // y축을 기준으로
		float angle_rad = XMConvertToRadians(rotAngle);  // 45도

		// 회전 쿼터니온 생성
		XMVECTOR quaternion = XMQuaternionRotationAxis(axisx, angle_rad);
		XMStoreFloat4(&rotation_, quaternion);
		isDirty_ = true; 
		timeStampSetter_ = TimerNow;
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
		timeStampSetter_ = TimerNow;
	}

	void TransformComponent::SetWorldMatrix(const XMFLOAT4X4& world)
	{
		float diff_sq = differenceMatrices(world_, world);
		world_ = world;
		if (diff_sq > FLT_EPSILON)
		{
			timeStampWorldUpdate_ = TimerNow;
		}
	}

	void TransformComponent::UpdateMatrix()
	{
		isDirty_ = false;

		if (!isMatrixAutoUpdate_)
		{
			// no update of local_ 
			XMMATRIX xL = XMLoadFloat4x4(&local_);
			XMVECTOR S, R, T;
			XMMatrixDecompose(&S, &R, &T, xL);
			XMStoreFloat3(&position_, T);
			XMStoreFloat4(&rotation_, R);
			XMStoreFloat3(&scale_, S);
			return;
		}

		XMVECTOR S_local = XMLoadFloat3(&scale_);
		XMVECTOR R_local = XMLoadFloat4(&rotation_);
		XMVECTOR T_local = XMLoadFloat3(&position_);

		XMFLOAT4X4 local;
		XMStoreFloat4x4(&local,
			XMMatrixScalingFromVector(S_local) *
			XMMatrixRotationQuaternion(R_local) *
			XMMatrixTranslationFromVector(T_local));
		float diff_sq = differenceMatrices(local_, local);
		local_ = local;
		if (diff_sq > FLT_EPSILON)
		{
			//timeStampWorldUpdate_ = TimerNow;
			compfactory::SetSceneComponentsDirty(entity_);
		}
	}
	void TransformComponent::UpdateWorldMatrix()
	{
		std::vector<HierarchyComponent*> hierarchies;
		HierarchyComponent* hierarchy = compfactory::GetHierarchyComponent(entity_);
		while (hierarchy)
		{
			hierarchies.push_back(hierarchy);
			Entity entity = compfactory::GetEntityByVUID(hierarchy->GetParent());
			HierarchyComponent* hierarchy_parent = compfactory::GetHierarchyComponent(entity);
			assert(hierarchy_parent != hierarchy);
			hierarchy = hierarchy_parent;
		}
		size_t n = hierarchies.size();
		if (n > 0)
		{
			XMMATRIX world = XMMatrixIdentity();
			for (size_t i = n; i > 0u; --i)
			{
				HierarchyComponent* hierarchy = hierarchies[i - 1];
				TransformComponent* transform = compfactory::GetTransformComponent(hierarchy->GetEntity());
				XMMATRIX local = XMMatrixIdentity();
				if (transform)
				{
					if (transform->IsDirty())
						transform->UpdateMatrix();
					XMFLOAT4X4 local_f44 = transform->GetLocalMatrix();
					local = XMLoadFloat4x4(&local_f44);
				}
				world = local * world;
				XMFLOAT4X4 mat_world;
				XMStoreFloat4x4(&mat_world, world);
				transform->SetWorldMatrix(mat_world);
			}
		}
		else
		{
			if (isDirty_)
			{
				UpdateMatrix();
			}
			SetWorldMatrix(local_);
		}
	}
}

namespace vz
{
	const XMVECTOR BASE_LIGHT_DIR = XMVectorSet(0, 0, -1, 0); // Forward direction
	inline void LightComponent::Update()
	{
		TransformComponent* transform = compfactory::GetTransformComponent(entity_);
		XMMATRIX W;
		if (transform != nullptr)
		{
			// note the transform must be updated!!
			XMFLOAT4X4 mat44 = transform->GetWorldMatrix();
			W = XMLoadFloat4x4(&mat44);

			TimeStamp transform_timeStamp = transform->GetTimeStamp();
			if (TimeDurationCount(transform_timeStamp, timeStampSetter_) > 0)
			{
				timeStampSetter_ = transform_timeStamp;
			}
		}
		else
		{
			W = XMLoadFloat4x4(&vz::math::IDENTITY_MATRIX);
		}
		XMVECTOR S, R, T;
		XMMatrixDecompose(&S, &R, &T, W);
		XMStoreFloat3(&position, T);
		XMStoreFloat4(&rotation, R);
		XMStoreFloat3(&scale, S);
		XMStoreFloat3(&direction, XMVector3Normalize(XMVector3TransformNormal(BASE_LIGHT_DIR, W)));

		occlusionquery = -1;	// TODO

		switch (type_)
		{
		default:
		case LightType::DIRECTIONAL:
			aabb_.createFromHalfWidth(XMFLOAT3(0, 0, 0), XMFLOAT3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()));
			break;
		case LightType::SPOT:
			aabb_.createFromHalfWidth(position, XMFLOAT3(range_, range_, range_));
			break;
		case LightType::POINT:
			aabb_.createFromHalfWidth(position, XMFLOAT3(range_, range_, range_));
			break;
		}

		isDirty_ = false;
	}
}
