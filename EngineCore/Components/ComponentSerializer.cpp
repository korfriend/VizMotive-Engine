#include "Components.h"
#include "Common/Archive.h"
#include "Utils/ECS.h"

namespace vz
{
	using namespace vz::ecs;

	void ComponentBase::serializeBase(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			cType_ = static_cast<ComponentType>(u8_data);
			archive >> vuid_;
		}
		else
		{
			archive << static_cast<uint8_t>(cType_);
			archive << vuid_;
		}
	}

	void NameComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		serializeBase(archive, version);
		if (archive.IsReadMode())
		{
			archive >> name;
		}
		else
		{
			archive << name;
		}
	}

	void TransformComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		serializeBase(archive, version);
		if (archive.IsReadMode())
		{
			archive >> isMatrixAutoUpdate_;
			archive >> scale_;
			archive >> rotation_;
			archive >> position_;

			UpdateMatrix();
		}
		else
		{
			archive << isMatrixAutoUpdate_; // maybe not needed just for dirtiness, but later might come handy if we have more persistent flags
			archive << scale_;
			archive << rotation_;
			archive << position_;
		}
	}

	void HierarchyComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		serializeBase(archive, version);
		if (archive.IsReadMode())
		{
			archive >> vuidParentHierarchy;
		}
		else
		{
			archive << vuidParentHierarchy;
		}
	}

	void MaterialComponent::Serialize(vz::Archive& archive)
	{
		serializeBase(archive, seri);
		if (archive.IsReadMode())
		{
			uint32_t u32_data;
			archive >> u32_data;
			shaderType = static_cast<ShaderType>(u32_data);
			archive >> renderOptionFlags_;
			archive >> baseColor_;
			archive >> specularColor_;
			archive >> emissiveColor_;
			archive >> phongFactors_;

			archive >> u32_data;
			assert(u32_data <= SCU32(TextureSlot::TEXTURESLOT_COUNT));
			for (uint32_t i = 0, n = u32_data; i < n; ++i)
			{
				SerializeEntity(archive, textureComponents_[i], seri);
			}
			isDirty_ = true;
		}
		else
		{
			archive << SCU32(shaderType);
			archive << renderOptionFlags_;
			archive << baseColor_;
			archive << specularColor_;
			archive << emissiveColor_;
			archive << phongFactors_;
			uint32_t tex_slot_count = SCU32(TextureSlot::TEXTURESLOT_COUNT);
			archive << tex_slot_count;
			for (uint32_t i = 0; i < tex_slot_count; ++i)
			{
				SerializeEntity(archive, textureComponents_[i], seri);
			}
		}
	}

	void GeometryComponent::Primitive::Serialize(vz::Archive& archive)
	{
		serializeBase(archive, seri);
		if (archive.IsReadMode())
		{
			archive >> vertexPositions_;
			archive >> vertexNormals_;
			archive >> vertexUVset0_;
			archive >> vertexUVset1_;
			archive >> vertexColors_;
			archive >> indexPrimitives_;
		}
		else
		{
			archive << vertexPositions_;
			archive << vertexNormals_;
			archive << vertexUVset0_;
			archive << vertexUVset1_;
			archive << vertexColors_;
			archive << indexPrimitives_;
		}
	}

	void GeometryComponent::Serialize(vz::Archive& archive)
	{
		serializeBase(archive, seri);
		if (archive.IsReadMode())
		{
			uint32_t u32_data;
			archive >> u32_data; // num of parts
			for (size_t i = 0, n = u32_data; i < n; ++i)
			{
				parts_[i].Serialize(archive, seri);
			}
			updateAABB();
			isDirty_ = true;
		}
		else
		{
			archive << parts_.size();
			for (size_t i = 0, n = parts_.size(); i < n; ++i)
			{
				parts_[i].Serialize(archive, seri);
			}
		}
	}

	void TextureComponent::Serialize(vz::Archive& archive)
	{
		serializeBase(archive, seri);
	}

	void RenderableComponent::Serialize(vz::Archive& archive)
	{
		serializeBase(archive, seri);
		if (archive.IsReadMode())
		{
			archive >> visibleLayerMask_;
			SerializeEntity(archive, geometryEntity_, seri);

			uint32_t u32_data;
			archive >> u32_data;
			for (size_t i = 0, n = u32_data; i < n; ++i)
			{
				SerializeEntity(archive, materialEntities_[i], seri);
			}
		}
		else
		{
			archive << visibleLayerMask_;
			SerializeEntity(archive, geometryEntity_, seri);

			archive << materialEntities_.size();
			for (size_t i = 0, n = materialEntities_.size(); i < n; ++i)
			{
				SerializeEntity(archive, materialEntities_[i], seri);
			}
		}
	}

	void LightComponent::Serialize(vz::Archive& archive)
	{
		serializeBase(archive, seri);
		if (archive.IsReadMode())
		{
			archive >> color_;
		}
		else
		{
			archive << color_;
		}
	}

	void CameraComponent::Serialize(vz::Archive& archive)
	{
		serializeBase(archive, seri);
		if (archive.IsReadMode())
		{
			archive >> zNearP_;
			archive >> zFarP_;
			archive >> fovY_;
			archive >> width_;
			archive >> height_;
			uint8_t u8_data;
			archive >> u8_data;
			projectionType_ = static_cast<enums::Projection>(u8_data);
		}
		else
		{
			archive << zNearP_;
			archive << zFarP_;
			archive << fovY_;
			archive << width_;
			archive << height_;
			archive << static_cast<uint8_t>(projectionType_);
		}
	}
}