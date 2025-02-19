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
	void HierarchyComponent::SetParent(const Entity entityParent)
	{
		HierarchyComponent* parent_hierarchy = compfactory::GetHierarchyComponent(entityParent);
		if (parent_hierarchy)
		{
			SetParent(parent_hierarchy->GetVUID());
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
		isDirty_ = false;

		if (!isMatrixAutoUpdate_)
		{
			XMMATRIX xL = XMLoadFloat4x4(&local_);
			XMVECTOR S, R, T;
			XMMatrixDecompose(&S, &R, &T, xL);
			XMStoreFloat3(&position_, T);
			XMStoreFloat4(&rotation_, R);
			XMStoreFloat3(&scale_, S);
			return;
		}
		else
		{
			XMVECTOR S_local = XMLoadFloat3(&scale_);
			XMVECTOR R_local = XMLoadFloat4(&rotation_);
			XMVECTOR T_local = XMLoadFloat3(&position_);
			XMStoreFloat4x4(&local_,
				XMMatrixScalingFromVector(S_local) *
				XMMatrixRotationQuaternion(R_local) *
				XMMatrixTranslationFromVector(T_local));
		}

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
#define MAX_MATERIAL_SLOT 10000

	// return 0 : NOT renderable
	// return 1 : Mesh renderable
	// return 2 : Volume renderable
	inline int checkIsRenderable(const VUID vuidGeo, const std::vector<VUID>& vuidMaterials)
	{
		int ret = 0;
		GeometryComponent* geo_comp = compfactory::GetGeometryComponent(compfactory::GetEntityByVUID(vuidGeo));
		if (geo_comp == nullptr)
		{
			if (vuidMaterials.size() == 1)
			{
				MaterialComponent* material = compfactory::GetMaterialComponent(compfactory::GetEntityByVUID(vuidMaterials[0]));
				if (material)
				{
					VUID vuid0 = material->GetVolumeTextureVUID(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP);
					VUID vuid1 = material->GetLookupTableVUID(MaterialComponent::LookupTableSlot::LOOKUP_OTF);
					bool hasRenderableVolume = compfactory::ContainVolumeComponent(compfactory::GetEntityByVUID(vuid0));
					bool hasLookupTable = compfactory::ContainTextureComponent(compfactory::GetEntityByVUID(vuid1));
					ret = (hasRenderableVolume && hasLookupTable) ? 2 : 0;
				}
			}
		}
		else
		{
			ret = (geo_comp->GetNumParts() == vuidMaterials.size() && geo_comp->GetNumParts() > 0) ? 1 : 0;
		}
		return ret;
	}

	void RenderableComponent::updateRenderableFlags()
	{
		switch (checkIsRenderable(vuidGeometry_, vuidMaterials_))
		{
		case 0: flags_ &= ~RenderableFlags::MESH_RENDERABLE; flags_ &= ~RenderableFlags::VOLUME_RENDERABLE; break;
		case 1: flags_ |= RenderableFlags::MESH_RENDERABLE; flags_ &= ~RenderableFlags::VOLUME_RENDERABLE; break;
		case 2: flags_ &= ~RenderableFlags::MESH_RENDERABLE; flags_ |= RenderableFlags::VOLUME_RENDERABLE; break;
		default:
			break;
		}
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

		Update();

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
		
		Update();

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

		Update();

		timeStampSetter_ = TimerNow;
	}

	Entity RenderableComponent::GetGeometry() const { return compfactory::GetEntityByVUID(vuidGeometry_); }
	Entity RenderableComponent::GetMaterial(const size_t slot) const { return slot >= vuidMaterials_.size() ? INVALID_ENTITY : compfactory::GetEntityByVUID(vuidMaterials_[slot]); }

	std::vector<Entity> RenderableComponent::GetMaterials() const
	{
		size_t n = vuidMaterials_.size();
		//entities.resize(n);
		std::vector<Entity> entities(n);
		for (size_t i = 0; i < n; ++i)
		{
			entities[i] = compfactory::GetEntityByVUID(vuidMaterials_[i]);
		}
		return entities;
	}

	size_t RenderableComponent::GetNumParts() const
	{
		GeometryComponent* geometry = compfactory::GetGeometryComponentByVUID(vuidGeometry_);
		if (geometry == nullptr)
		{
			return 0;
		}
		return geometry->GetNumParts();
	}

	size_t RenderableComponent::GetMaterials(Entity* entities) const
	{
		size_t n = vuidMaterials_.size();
		for (size_t i = 0; i < n; ++i)
		{
			entities[i] = compfactory::GetEntityByVUID(vuidMaterials_[i]);
		}
		return n;
	}

	void RenderableComponent::Update()
	{
		updateRenderableFlags();

		// compute AABB
		Entity geometry_entity = compfactory::GetEntityByVUID(vuidGeometry_);
		GeometryComponent* geometry = compfactory::GetGeometryComponent(geometry_entity);
		TransformComponent* transform = compfactory::GetTransformComponent(entity_);
		if (IsVolumeRenderable())
		{
			MaterialComponent* volume_material = compfactory::GetMaterialComponent(compfactory::GetEntityByVUID(vuidMaterials_[0]));
			assert(volume_material);
			VUID volume_vuid = volume_material->GetVolumeTextureVUID(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP);
			VolumeComponent* volume = compfactory::GetVolumeComponent(compfactory::GetEntityByVUID(volume_vuid));

			XMFLOAT4X4 world = transform->GetWorldMatrix();
			XMMATRIX W = XMLoadFloat4x4(&world);
			aabb_ = volume->ComputeAABB().transform(W);
		}
		else
		{
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
		}

		timeStampSetter_ = TimerNow;
		isDirty_ = false;
	}
}

namespace vz
{
	const XMVECTOR BASE_LIGHT_DIR = XMVectorSet(0, 0, 1, 0);
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
	inline float computeInverseLineardepth(const float lin, const float znear, const float zfar)
	{
		float z_n = ((lin - 2 * zfar) * znear + zfar * lin) / (lin * znear - zfar * lin);
		float z = (z_n + 1) * 0.5f;
		return z;
	}
	float CameraComponent::computeOrthoVerticalSizeFromPerspective(const float dist)
	{
		float z = computeInverseLineardepth(std::abs(dist), zNearP_, zFarP_);
		XMMATRIX P = VZMatrixPerspectiveFov(fovY_, width_ / height_, zFarP_, zNearP_); // reverse zbuffer!
		XMMATRIX Unproj = XMMatrixInverse(nullptr, P);
		XMVECTOR Ptop = XMVector3TransformCoord(XMVectorSet(0, 1, z, 1), Unproj);
		XMVECTOR Pbottom = XMVector3TransformCoord(XMVectorSet(0, -1, z, 1), Unproj);
		return XMVectorGetX(XMVector3Length(Ptop - Pbottom));
	}

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
		XMStoreFloat3(&forward_, XMVector3Normalize(-XMLoadFloat3(&z_axis)));

		isDirty_ = true;
		timeStampSetter_ = TimerNow;
		return true;
	}

	void CameraComponent::SetWorldLookAt(const XMFLOAT3& eye, const XMFLOAT3& at, const XMFLOAT3& up)
	{
		eye_ = eye; at_ = at; up_ = up; XMStoreFloat3(&forward_, XMVector3Normalize(XMLoadFloat3(&at) - XMLoadFloat3(&eye)));
		isDirty_ = true;

		TransformComponent* tr_comp = compfactory::GetTransformComponent(entity_);
		if (tr_comp)
		{
			XMVECTOR _Eye = XMLoadFloat3(&eye_);
			XMVECTOR _At = XMLoadFloat3(&at_);
			XMVECTOR _Up = XMLoadFloat3(&up_);

			XMVECTOR _Dir = _At - _Eye;
			XMVECTOR _Right = XMVector3Cross(_Dir, _Up);
			_Up = XMVector3Cross(_Right, _Dir);
			_Up = XMVector3Normalize(_Up);

			XMMATRIX _V = VZMatrixLookAt(_Eye, _At, _Up);	// world
			XMMATRIX world = XMMatrixInverse(NULL, _V);

			XMMATRIX parent2ws = XMMatrixIdentity();
			HierarchyComponent* parent = compfactory::GetHierarchyComponent(entity_);
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

			// world = X * P
			XMMATRIX local = world * XMMatrixInverse(NULL, parent2ws);
			XMFLOAT4X4 mat_local;
			XMStoreFloat4x4(&mat_local, local);
			tr_comp->SetMatrix(mat_local);
		}
		
		timeStampSetter_ = TimerNow;
	}

	void CameraComponent::SetPerspective(const float width, const float height, const float nearP, const float farP, const float fovY) 
	{
		width_ = width; height_ = height; zNearP_ = nearP; zFarP_ = farP; fovY_ = fovY;
		flags_ &= ~ORTHOGONAL;
		isDirty_ = true; 
		timeStampSetter_ = TimerNow;
	}
	void CameraComponent::SetOrtho(const float width, const float height, const float nearP, const float farP, const float orthoVerticalSize)
	{
		width_ = width / height; height_ = 1.f; zNearP_ = nearP; zFarP_ = farP;

		// NOTE: When this function is called for the first time, 
		//	it will always enter the next branch
		if (!(flags_ & ORTHOGONAL)) // when previous setting was perspective
		{
			if (orthoVerticalSize < 0)
				orthoVerticalSize_ = computeOrthoVerticalSizeFromPerspective(math::Length(eye_));
			else if (orthoVerticalSize > 0)
				orthoVerticalSize_ = orthoVerticalSize;
		}

		flags_ |= ORTHOGONAL;

		{
			if (orthoVerticalSize > 0)
			{
				orthoVerticalSize_ = orthoVerticalSize;
			}
			width_ *= orthoVerticalSize_;
			height_ = orthoVerticalSize_;
		}
		isDirty_ = true; timeStampSetter_ = TimerNow;
	}

	void CameraComponent::UpdateMatrix()
	{
		if (flags_ != CUSTOM_PROJECTION)
		{
			XMMATRIX P;

			if (IsOrtho())
			{
				float aspect = width_ / height_;
				float ortho_width = orthoVerticalSize_ * aspect;
				float ortho_height = orthoVerticalSize_;
				P = VZMatrixOrthographic(ortho_width, ortho_height, zFarP_, zNearP_); // reverse zbuffer!
			}
			else
			{
				P = VZMatrixPerspectiveFov(fovY_, width_ / height_, zFarP_, zNearP_); // reverse zbuffer!
			}

			P = P * XMMatrixTranslation(jitter.x, jitter.y, 0);
			XMStoreFloat4x4(&projection_, P); // reverse zbuffer!
		}

		XMVECTOR _Eye = XMLoadFloat3(&eye_);
		XMVECTOR _At = XMLoadFloat3(&at_);
		XMVECTOR _Up = XMLoadFloat3(&up_);

		XMVECTOR _Dir = _At - _Eye;
		XMVECTOR _Right = XMVector3Cross(_Dir, _Up);
		_Up = XMVector3Cross(_Right, _Dir);
		_Up = XMVector3Normalize(_Up);

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