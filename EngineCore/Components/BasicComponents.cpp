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
	static bool isInitialized = false;
	uint64_t generateUUID(uint8_t cType) 
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

		// Atomic counter (8 bits)
		uint16_t count = sCounter.fetch_add(1, std::memory_order_relaxed);
		uuid |= (count & 0xFF) << 8;

		// Random component (8 bits)
		//uint8_t randomBits = static_cast<uint8_t>(sRandomEngine() & 0xFF);
		//uuid |= static_cast<uint64_t>(randomBits);
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

	void HierarchyComponent::SetParent(const VUID vuidParent)
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

		compfactory::SetSceneComponentsDirty(entity_);
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
			if (isDirty_) UpdateMatrix();
			SetWorldMatrix(local_);
		}
	}
}

namespace vz
{
#define MAX_MATERIAL_SLOT 10000

	bool checkValidity(const VUID vuidGeo, const std::vector<VUID>& vuidMaterials)
	{
		GeometryComponent* geo_comp = compfactory::GetGeometryComponent(compfactory::GetEntityByVUID(vuidGeo));
		if (geo_comp == nullptr) return false;
		return geo_comp->GetNumParts() == vuidMaterials.size() && geo_comp->GetNumParts() > 0;
	}
	void RenderableComponent::SetGeometry(const Entity geometryEntity)
	{
		GeometryComponent* geo_comp = compfactory::GetGeometryComponent(geometryEntity);
		if (geo_comp == nullptr)
		{
			backlog::post("invalid entity", backlog::LogLevel::Error);
			return;
		}

		GRenderableComponent* downcast = (GRenderableComponent*)this;

		vuidGeometry_ = geo_comp->GetVUID();
		if (geo_comp->GetNumParts() == vuidMaterials_.size() && compfactory::GetTransformComponent(entity_)
			&& geo_comp->GetNumParts() > 0)
		{
			flags_ |= SCU32(RenderableFlags::RENDERABLE);
		}
		else
		{
			flags_ &= ~SCU32(RenderableFlags::RENDERABLE);
		}
		timeStampSetter_ = TimerNow;
	}
	void RenderableComponent::SetMaterial(const Entity materialEntity, const size_t slot)
	{
		assert(slot <= MAX_MATERIAL_SLOT);
		MaterialComponent* mat_comp = compfactory::GetMaterialComponent(materialEntity);
		if (mat_comp == nullptr)
		{
			backlog::post("invalid entity", backlog::LogLevel::Error);
			return;
		}
		if (slot >= vuidMaterials_.size())
		{
			std::vector<VUID> vuidMaterials_temp = vuidMaterials_;
			vuidMaterials_.assign(slot + 1, INVALID_VUID);
			if (vuidMaterials_temp.size() > 0)
			{
				memcpy(&vuidMaterials_[0], &vuidMaterials_temp[0], sizeof(VUID) * vuidMaterials_temp.size());
			}
		}
		vuidMaterials_[slot] = mat_comp->GetVUID();
		if (checkValidity(vuidGeometry_, vuidMaterials_))
		{
			flags_ |= SCU32(RenderableFlags::RENDERABLE);
		}
		else
		{
			flags_ &= ~SCU32(RenderableFlags::RENDERABLE);
		}

		timeStampSetter_ = TimerNow;
	}
	void RenderableComponent::SetMaterials(const std::vector<Entity>& materials)
	{
		vuidMaterials_.clear();
		size_t n = materials.size();
		vuidMaterials_.reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			MaterialComponent* mat_comp = compfactory::GetMaterialComponent(materials[i]);
			vuidMaterials_.push_back(mat_comp->GetVUID());
		}
		if (checkValidity(vuidGeometry_, vuidMaterials_))
		{
			flags_ |= SCU32(RenderableFlags::RENDERABLE);
		}
		else
		{
			flags_ &= ~SCU32(RenderableFlags::RENDERABLE);
		}

		timeStampSetter_ = TimerNow;
	}

	Entity RenderableComponent::GetGeometry() const { return compfactory::GetEntityByVUID(vuidGeometry_); }
	Entity RenderableComponent::GetMaterial(const size_t slot) const { return slot >= vuidMaterials_.size() ? INVALID_ENTITY : compfactory::GetEntityByVUID(vuidMaterials_[slot]); }
	std::vector<Entity> RenderableComponent::GetMaterials() const
	{
		size_t n = vuidMaterials_.size();
		std::vector<Entity> entities(n);
		for (size_t i = 0; i < n; ++i)
		{
			entities[i] = compfactory::GetEntityByVUID(vuidMaterials_[i]);
		}
		return entities;
	}

	void RenderableComponent::Update()
	{
		// compute AABB
		Entity geometry_entity = compfactory::GetEntityByVUID(vuidGeometry_);
		GeometryComponent* geometry = compfactory::GetGeometryComponent(geometry_entity);
		TransformComponent* transform = compfactory::GetTransformComponent(entity_);
		if (geometry == nullptr || transform == nullptr)
		{
			return;
		}
		if (geometry->GetNumParts() == 0)
		{
			return;
		}

		XMFLOAT4X4 world = transform->GetWorldMatrix();
		XMMATRIX W = XMLoadFloat4x4(&world);
		aabb_ = geometry->GetAABB().transform(W);
		isDirty_ = false;
	}
}

namespace vz
{
	const XMVECTOR BASE_LIGHT_DIR = XMVectorSet(0, 1, 0, 0);
	inline void LightComponent::Update()
	{
		TransformComponent* transform = compfactory::GetTransformComponent(entity_);
		XMMATRIX W;
		if (transform != nullptr)
		{
			// note the transform must be updated!!
			XMFLOAT4X4 mat44 = transform->GetWorldMatrix();
			W = XMLoadFloat4x4(&mat44);
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
		timeStampSetter_ = TimerNow;
	}
}

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
			XMFLOAT4X4 local_f44 = tr_comp->GetLocalMatrix();
			local = XMLoadFloat4x4(&local_f44);
		}

		XMMATRIX parent2ws = XMMatrixIdentity();
		while (parent)
		{
			TransformComponent* transform_parent = compfactory::GetTransformComponent(compfactory::GetEntityByVUID(parent->GetParent()));
			if (transform_parent)
			{
				if (transform_parent->IsDirty()) 
					transform_parent->UpdateMatrix();
				XMFLOAT4X4 local_f44 = transform_parent->GetLocalMatrix();
				parent2ws *= XMLoadFloat4x4(&local_f44);
			}
			parent = compfactory::GetHierarchyComponent(compfactory::GetEntityByVUID(parent->GetParent()));
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
		return true;
	}
	void CameraComponent::UpdateMatrix()
	{
		if (projectionType_ != Projection::CUSTOM_PROJECTION)
		{
			XMStoreFloat4x4(&projection_, VZMatrixPerspectiveFov(fovY_, width_ / height_, zFarP_, zNearP_)); // reverse zbuffer!
			projection_.m[2][0] = jitter.x;
			projection_.m[2][1] = jitter.y;
		}

		XMVECTOR _Eye = XMLoadFloat3(&eye_);
		XMVECTOR _At = XMLoadFloat3(&at_);
		XMVECTOR _Up = XMLoadFloat3(&up_);

		XMMATRIX _V = VZMatrixLookAt(_Eye, _At, _Up);
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