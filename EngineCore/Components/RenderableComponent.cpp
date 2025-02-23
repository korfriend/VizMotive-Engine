#include "GComponents.h"
#include "Utils/Backlog.h"

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
			vzlog_error("Invalid geometry entity (%d)", geometryEntity);
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
			vzlog_error("Invalid material entity (%d)", materialEntity);
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