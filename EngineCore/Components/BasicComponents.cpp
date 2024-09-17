#include "Components.h"
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
	static bool isInitialized = false;
	uint64_t generateUUID() 
	{
		if (!isInitialized)
		{
			std::random_device rd;
			sRandomEngine.seed(rd());
			isInitialized = true;
		}
		uint64_t uuid = 0;

		// High precision timestamp (48 bits)
		auto now = std::chrono::system_clock::now();
		auto duration = now.time_since_epoch();
		auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
		uint64_t timestamp = microseconds.count() & 0xFFFFFFFFFFFFull;
		uuid |= timestamp << 16;

		// Random component (8 bits)
		uint8_t randomBits = static_cast<uint8_t>(sRandomEngine() & 0xFF);
		uuid |= static_cast<uint64_t>(randomBits) << 8;

		// Atomic counter (8 bits)
		uint16_t count = sCounter.fetch_add(1, std::memory_order_relaxed);
		uuid |= (count & 0xFF);

		return uuid;
	}
}

namespace vz
{
	ComponentBase::ComponentBase(const ComponentType compType, const Entity entity) : cType_(compType), entity_(entity)
	{
		vuid_ = uuid::generateUUID();
		timeStampSetter_ = TimerNow;
	}

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

		timeStampSetter_ = TimerNow;
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
		timeStampSetter_ = TimerNow;
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
		timeStampSetter_ = TimerNow;
	}
	void TransformComponent::UpdateWorldMatrix()
	{
		std::vector<HierarchyComponent*> hierarchies;
		HierarchyComponent* hierarchy = compfactory::GetHierarchyComponent(entity_);
		while (hierarchy)
		{
			hierarchies.push_back(hierarchy);
			HierarchyComponent* hierarchy_parent = compfactory::GetHierarchyComponent(hierarchy->parentEntity);
			assert(hierarchy_parent != hierarchy);
			hierarchy = hierarchy_parent;
		}
		size_t n = hierarchies.size();
		if (n > 0)
		{
			XMMATRIX world = XMMatrixIdentity();
			for (size_t i = n - 1; i >= 0u; --i)
			{
				HierarchyComponent* hierarchy = hierarchies[i];
				TransformComponent* transform = compfactory::GetTransformComponent(hierarchy->GetEntity());
				XMMATRIX local = XMMatrixIdentity();
				if (transform)
				{
					if (transform->IsDirty())
						transform->UpdateMatrix();
					local = XMLoadFloat4x4(&transform->GetLocalMatrix());
				}
				world = local * world;
				XMFLOAT4X4 mat_world;
				XMStoreFloat4x4(&mat_world, world);
				transform->SetWorldMatrix(mat_world);
			}
		}
		else
		{
			if (isDirty_) UpdateMatrix();
			SetWorldMatrix(local_);
		}
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
/*
void Scene::RunHierarchyUpdateSystem(wi::jobsystem::context& ctx)
{
	wi::jobsystem::Dispatch(ctx, (uint32_t)hierarchy.GetCount(), small_subtask_groupsize, [&](wi::jobsystem::JobArgs args) {

		HierarchyComponent& hier = hierarchy[args.jobIndex];
		Entity entity = hierarchy.GetEntity(args.jobIndex);

		TransformComponent* transform_child = transforms.GetComponent(entity);
		XMMATRIX worldmatrix;
		if (transform_child != nullptr)
		{
			worldmatrix = transform_child->GetLocalMatrix();
		}

		LayerComponent* layer_child = layers.GetComponent(entity);
		if (layer_child != nullptr)
		{
			layer_child->propagationMask = ~0u; // clear propagation mask to full
		}

		if (transform_child == nullptr && layer_child == nullptr)
			return;

		Entity parentID = hier.parentID;
		while (parentID != INVALID_ENTITY)
		{
			TransformComponent* transform_parent = transforms.GetComponent(parentID);
			if (transform_child != nullptr && transform_parent != nullptr)
			{
				worldmatrix *= transform_parent->GetLocalMatrix();
			}

			LayerComponent* layer_parent = layers.GetComponent(parentID);
			if (layer_child != nullptr && layer_parent != nullptr)
			{
				layer_child->propagationMask &= layer_parent->layerMask;
			}

			const HierarchyComponent* hier_recursive = hierarchy.GetComponent(parentID);
			if (hier_recursive != nullptr)
			{
				parentID = hier_recursive->parentID;
			}
			else
			{
				parentID = INVALID_ENTITY;
			}
		}

		if (transform_child != nullptr)
		{
			XMStoreFloat4x4(&transform_child->world, worldmatrix);
		}

		});
}
/**/
namespace vz
{
	bool CameraComponent::SetWorldLookAtFromHierarchyTransforms()
	{
		TransformComponent* tr_comp = compfactory::GetTransformComponent(entity_);
		HierarchyComponent* parent = compfactory::GetHierarchyComponent(entity_);
		XMMATRIX local;
		if (tr_comp == nullptr)
		{
			if (parent == nullptr)
			{
				return false;
			}
			local = XMMatrixIdentity();
		}
		else
		{
			if (tr_comp->IsDirty()) 
				tr_comp->UpdateMatrix();
			local = XMLoadFloat4x4(&tr_comp->GetLocalMatrix());
		}

		XMMATRIX parent2ws = XMMatrixIdentity();
		while (parent)
		{
			TransformComponent* transform_parent = compfactory::GetTransformComponent(parent->parentEntity);
			if (transform_parent)
			{
				if (transform_parent->IsDirty()) 
					transform_parent->UpdateMatrix();
				parent2ws *= XMLoadFloat4x4(&transform_parent->GetLocalMatrix());
			}
			parent = compfactory::GetHierarchyComponent(parent->parentEntity);
		}
		XMFLOAT4X4 mat_world;
		XMStoreFloat4x4(&mat_world, local * parent2ws);
		tr_comp->SetWorldMatrix(mat_world);

		eye_ = *((XMFLOAT3*)&mat_world._41);
		up_ = vz::math::GetUp(mat_world);
		XMFLOAT3 z_axis = vz::math::GetForward(mat_world);
		XMVECTOR _At = XMLoadFloat3(&eye_) - XMLoadFloat3(&z_axis);
		XMStoreFloat3(&at_, _At);

		isDirty_ = true;
		timeStampSetter_ = TimerNow;
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