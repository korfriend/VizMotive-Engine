#include "GComponents.h"
#include "Utils/Backlog.h"

namespace vz
{
#define MAX_MATERIAL_SLOT 10000

	using PrimitiveType = GeometryComponent::PrimitiveType;
	using RenderableType = RenderableComponent::RenderableType;
	// return 0 : NOT renderable
	// return 1 : Mesh renderable
	// return 2 : Volume renderable
	
	inline RenderableType checkIsRenderable(const Entity entityRenderable, const VUID vuidGeo, const std::vector<VUID>& vuidMaterials)
	{
		RenderableType ret = RenderableType::UNDEFINED;
		GGeometryComponent* geomertry = (GGeometryComponent*)compfactory::GetGeometryComponent(compfactory::GetEntityByVUID(vuidGeo));
		if (geomertry == nullptr)
		{
			if (vuidMaterials.size() == 1)
			{
				MaterialComponent* material = compfactory::GetMaterialComponent(compfactory::GetEntityByVUID(vuidMaterials[0]));
				if (material)
				{
					VUID vuid0 = material->GetVolumeTextureVUID(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP);
					VUID vuid1 = material->GetLookupTableVUID(MaterialComponent::LookupTableSlot::LOOKUP_OTF);
					VUID vuid2 = material->GetLookupTableVUID(MaterialComponent::LookupTableSlot::LOOKUP_WINDOWING);
					bool hasRenderableVolume = compfactory::ContainVolumeComponent(compfactory::GetEntityByVUID(vuid0));
					bool hasLookupTable1 = compfactory::ContainTextureComponent(compfactory::GetEntityByVUID(vuid1));
					bool hasLookupTable2 = compfactory::ContainTextureComponent(compfactory::GetEntityByVUID(vuid2));
					if (hasRenderableVolume && (hasLookupTable1 || hasLookupTable2))
					{
						ret = RenderableType::VOLUME_RENDERABLE;
					}
				}
			}

			if (ret != RenderableType::VOLUME_RENDERABLE)
			{
				if (compfactory::ContainSpriteComponent(entityRenderable))
				{
					ret = RenderableType::SPRITE_RENDERABLE;
				}
				else if (compfactory::ContainSpriteFontComponent(entityRenderable))
				{
					ret = RenderableType::SPRITEFONT_RENDERABLE;
				}
			}
		}
		else
		{
			size_t num_parts = geomertry->GetNumParts();
			size_t num_mats = vuidMaterials.size();
			if (num_parts == num_mats && num_parts > 0)
			{
				if (geomertry->allowGaussianSplatting)
				{
					ret = RenderableType::GSPLAT_RENDERABLE;
				}
				else
				{
					ret = RenderableType::MESH_RENDERABLE;
				}
			}
			else
			{
				vzlog_warning("Not Renderable--> # of Parts: %d != # of Materials: %d", (int)num_parts, (int)num_mats);
			}
		}
		return ret;
	}

	void RenderableComponent::updateRenderableFlags()
	{
		RenderableType r_type = checkIsRenderable(entity_, vuidGeometry_, vuidMaterials_);
		if (r_type == renderableReservedType_ || renderableReservedType_ == RenderableType::ALLTYPES_RENDERABLE)
		{
			renderableType_ = r_type;
		}
		else
		{
			renderableType_ = RenderableType::UNDEFINED;
		}
	}

	void RenderableComponent::SetGeometry(const Entity geometryEntity)
	{
		GeometryComponent* geo_comp = compfactory::GetGeometryComponent(geometryEntity);
		if (geo_comp == nullptr)
		{
			vzlog_error("Invalid geometry entity (%llu)", geometryEntity);
			return;
		}

		GRenderableComponent* downcast = (GRenderableComponent*)this;

		vuidGeometry_ = geo_comp->GetVUID();

		isDirty_ = true;

		timeStampSetter_ = TimerNow;
	}
	void RenderableComponent::SetMaterial(const Entity materialEntity, const size_t slot)
	{
		assert(slot <= MAX_MATERIAL_SLOT);
		MaterialComponent* mat_comp = compfactory::GetMaterialComponent(materialEntity);
		if (mat_comp == nullptr)
		{
			vzlog_error("Invalid material entity (%llu)", materialEntity);
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

		isDirty_ = true;

		timeStampSetter_ = TimerNow;
	}
	void RenderableComponent::SetMaterials(const std::vector<Entity>& materials)
	{
		vuidMaterials_.clear();
		size_t n = materials.size();
		vuidMaterials_.resize(n);
		for (size_t i = 0; i < n; ++i)
		{
			MaterialComponent* mat_comp = compfactory::GetMaterialComponent(materials[i]);
			vuidMaterials_[i] = mat_comp->GetVUID();
		}

		isDirty_ = true;

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
		if (!IsRenderable())
		{
			return;
		}

		// compute AABB
		Entity geometry_entity = compfactory::GetEntityByVUID(vuidGeometry_);
		GeometryComponent* geometry = compfactory::GetGeometryComponent(geometry_entity);
		TransformComponent* transform = compfactory::GetTransformComponent(entity_);
		geometrics::AABB aabb;
		if (GetRenderableType() == RenderableType::VOLUME_RENDERABLE)
		{
			MaterialComponent* volume_material = compfactory::GetMaterialComponent(compfactory::GetEntityByVUID(vuidMaterials_[0]));
			assert(volume_material);
			VUID volume_vuid = volume_material->GetVolumeTextureVUID(MaterialComponent::VolumeTextureSlot::VOLUME_MAIN_MAP);
			VolumeComponent* volume = compfactory::GetVolumeComponent(compfactory::GetEntityByVUID(volume_vuid));

			XMFLOAT4X4 world = transform->GetWorldMatrix();
			XMMATRIX W = XMLoadFloat4x4(&world);
			aabb = volume->ComputeAABB().transform(W);
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
			aabb = geometry->GetAABB().transform(W);
		}

		if (math::DistanceSquared(aabb_._min, aabb._min) > FLT_EPSILON
			|| math::DistanceSquared(aabb_._max, aabb._max) > FLT_EPSILON)
		{
			timeStampSetter_ = TimerNow;
		}
		aabb_ = aabb;
		isDirty_ = false;
	}
}