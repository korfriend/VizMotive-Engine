#include "Components.h"
#include "Common/Archive.h"
#include "Utils/ECS.h"

namespace vz
{
	using namespace vz::ecs;

	void NameComponent::Serialize(vz::Archive& archive, const uint64_t version) 
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> name;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_
			
			archive << name;
		}
	}

	void TransformComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> isMatrixAutoUpdate_;
			archive >> scale_;
			archive >> rotation_;
			archive >> position_;

			UpdateMatrix();
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << isMatrixAutoUpdate_; // maybe not needed just for dirtiness, but later might come handy if we have more persistent flags
			archive << scale_;
			archive << rotation_;
			archive << position_;
		}
	}

	void HierarchyComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> vuidParentHierarchy_;
			archive >> childrenCache_;

			children_.clear();
			for (auto it : childrenCache_)
			{
				children_.insert(it);
			}
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << vuidParentHierarchy_;
			archive << childrenCache_;
		}
	}

	void MaterialComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			uint32_t u32_data;
			archive >> u32_data;
			shaderType_ = static_cast<ShaderType>(u32_data);
			archive >> renderOptionFlags_;
			archive >> u32_data;
			blendMode_ = static_cast<BlendMode>(u32_data);
			archive >> baseColor_;
			archive >> specularColor_;
			archive >> emissiveColor_;
			archive >> phongFactors_;

			// need to check version 
			archive >> u32_data;
			assert(u32_data <= SCU32(TextureSlot::TEXTURESLOT_COUNT));
			for (uint32_t i = 0, n = u32_data; i < n; ++i)
			{
				archive >> textureComponents_[i];
			}
			archive >> texMulAdd_;
			isDirty_ = true;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << SCU32(shaderType_);
			archive << renderOptionFlags_;
			archive << SCU32(blendMode_);
			archive << baseColor_;
			archive << specularColor_;
			archive << emissiveColor_;
			archive << phongFactors_;
			uint32_t tex_slot_count = SCU32(TextureSlot::TEXTURESLOT_COUNT);
			archive << tex_slot_count;
			for (uint32_t i = 0; i < tex_slot_count; ++i)
			{
				archive << textureComponents_[i];
			}
			archive << texMulAdd_;
		}
	}

	void GeometryComponent::Primitive::Serialize(vz::Archive& archive, const uint64_t version)
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

	void GeometryComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			uint32_t u32_data;
			archive >> u32_data; // num of parts
			for (size_t i = 0, n = u32_data; i < n; ++i)
			{
				parts_[i].Serialize(archive, version);
			}
			updateAABB();
			isDirty_ = true;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << parts_.size();
			for (size_t i = 0, n = parts_.size(); i < n; ++i)
			{
				parts_[i].Serialize(archive, version);
			}
		}
	}

	void TextureComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> width_;
			archive >> height_;
			archive >> depth_;
			archive >> arraySize_;
			archive >> vuidBindingGeometry_;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << width_;
			archive << height_;
			archive << depth_;
			archive << arraySize_;
			archive << vuidBindingGeometry_;	
		}
	}

	void RenderableComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> visibleLayerMask_;
			archive >> vuidGeometry_;

			uint32_t u32_data;
			archive >> u32_data;
			for (size_t i = 0, n = u32_data; i < n; ++i)
			{
				archive >> vuidMaterials_[i];
			}
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << visibleLayerMask_;
			archive << vuidGeometry_;

			archive << vuidMaterials_.size();
			for (size_t i = 0, n = vuidMaterials_.size(); i < n; ++i)
			{
				archive << vuidMaterials_[i];
			}
		}
	}

	void LightComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> color_;
			archive >> range_;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << color_;
			archive << range_;
		}
	}

	void CameraComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> zNearP_;
			archive >> zFarP_;
			archive >> fovY_;
			archive >> focalLength_;
			archive >> apertureSize_;
			archive >> apertureShape_;

			archive >> width_;
			archive >> height_;

			archive >> u8_data;
			projectionType_ = static_cast<Projection>(u8_data);

			archive >> visibleLayerMask_;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << zNearP_;
			archive << zFarP_;
			archive << fovY_;
			archive << focalLength_;
			archive << apertureSize_;
			archive << apertureShape_;

			archive << width_;
			archive << height_;
			archive << static_cast<uint8_t>(projectionType_);
			archive << visibleLayerMask_;
		}
	}
}