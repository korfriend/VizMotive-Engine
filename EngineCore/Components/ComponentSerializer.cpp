#include "Components.h"
#include "Common/Archive.h"
#include "Utils/ECS.h"

namespace vz
{
	using namespace vz::ecs;

	void NameComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
		if (archive.IsReadMode())
		{
			archive >> name;
		}
		else
		{
			archive << name;
		}
	}

	void TransformComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
		if (archive.IsReadMode())
		{
			archive >> isMatrixAutoUpdate_;
			archive >> scale_;
			archive >> rotation_;
			archive >> position_;
			archive >> local_;

			isDirty_ = true;
			if (isMatrixAutoUpdate_)
			{
				UpdateMatrix();
			}
		}
		else
		{
			archive << isMatrixAutoUpdate_; // maybe not needed just for dirtiness, but later might come handy if we have more persistent flags
			archive << scale_;
			archive << rotation_;
			archive << local_;
		}
	}

	void HierarchyComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
		SerializeEntity(archive, parentEntity, seri);
	}

	void MaterialComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
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
				SerializeEntity(archive, textures_[i], seri);
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
				SerializeEntity(archive, textures_[i], seri);
			}
		}
	}

	void GeometryComponent::Primitive::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
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

	void GeometryComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
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

	void TextureComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}

	void RenderableComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}

	void LightComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}

	void CameraComponent::Serialize(vz::Archive& archive, EntitySerializer& seri)
	{
	}
}