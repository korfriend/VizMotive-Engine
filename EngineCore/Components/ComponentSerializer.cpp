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
			archive >> flags_;
			archive >> u32_data;
			blendMode_ = static_cast<BlendMode>(u32_data);
			archive >> baseColor_;
			archive >> specularColor_;
			archive >> emissiveColor_;
			archive >> phongFactors_;
			archive >> metalness_;
			archive >> roughness_;
			archive >> alphaRef_;
			archive >> u32_data;
			engineStencilRef_ = static_cast<StencilRef>(u32_data);

			// need to check version 
			archive >> u32_data;
			assert(u32_data <= SCU32(TextureSlot::TEXTURESLOT_COUNT));
			for (uint32_t i = 0, n = u32_data; i < n; ++i)
			{
				archive >> textureComponents_[i];
			}

			archive >> u32_data;
			assert(u32_data <= SCU32(VolumeTextureSlot::VOLUME_TEXTURESLOT_COUNT));
			for (uint32_t i = 0, n = u32_data; i < n; ++i)
			{
				archive >> volumeComponents_[i];
			}

			archive >> u32_data;
			assert(u32_data <= SCU32(LookupTableSlot::LOOKUP_COUNT));
			for (uint32_t i = 0, n = u32_data; i < n; ++i)
			{
				archive >> lookupComponents_[i];
			}

			archive >> texMulAdd_;
			isDirty_ = true;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << SCU32(shaderType_);
			archive << flags_;
			archive << SCU32(blendMode_);
			archive << baseColor_;
			archive << specularColor_;
			archive << emissiveColor_;
			archive << phongFactors_;
			archive << metalness_;
			archive << roughness_;
			archive << alphaRef_;
			archive << SCU32(engineStencilRef_);

			uint32_t tex_slot_count = SCU32(TextureSlot::TEXTURESLOT_COUNT);
			archive << tex_slot_count;
			for (uint32_t i = 0; i < tex_slot_count; ++i)
			{
				archive << textureComponents_[i];
			}
			tex_slot_count = SCU32(VolumeTextureSlot::VOLUME_TEXTURESLOT_COUNT);
			archive << tex_slot_count;
			for (uint32_t i = 0; i < tex_slot_count; ++i)
			{
				archive << volumeComponents_[i];
			}
			tex_slot_count = SCU32(LookupTableSlot::LOOKUP_COUNT);
			archive << tex_slot_count;
			for (uint32_t i = 0; i < tex_slot_count; ++i)
			{
				archive << lookupComponents_[i];
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
			archive >> vertexTangents_;
			archive >> vertexUVset0_;
			archive >> vertexUVset1_;
			archive >> vertexColors_;
			archive >> indexPrimitives_;
			uint32_t data32t;
			archive >> data32t;
			ptype_ = static_cast<PrimitiveType>(data32t);
		}
		else
		{
			archive << vertexPositions_;
			archive << vertexNormals_;
			archive << vertexTangents_;
			archive << vertexUVset0_;
			archive << vertexUVset1_;
			archive << vertexColors_;
			archive << indexPrimitives_;
			archive << SCU32(ptype_);
		}
	}

	void GeometryComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_
			
			archive >> tessellationFactor_;
			uint32_t u32_data;
			archive >> u32_data; // num of parts
			for (size_t i = 0, n = u32_data; i < n; ++i)
			{
				parts_[i].Serialize(archive, version);
			}

			update();
			isDirty_ = true;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << tessellationFactor_;
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

			archive >> u8_data;
			textureType_ = static_cast<TextureType>(u8_data);
			archive >> u8_data;
			textureFormat_ = static_cast<TextureFormat>(u8_data);
			archive >> width_;
			archive >> height_;
			archive >> depth_;
			archive >> arraySize_;
			archive >> resName_;
			archive >> stride_;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << SCU8(textureType_);
			archive << SCU8(textureFormat_);
			archive << width_;
			archive << height_;
			archive << depth_;
			archive << arraySize_;
			archive << resName_;
			archive << stride_;
		}
	}

	void VolumeComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		TextureComponent::Serialize(archive, version);
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> voxelSize_;
			archive >> u8_data;
			volFormat_ = static_cast<VolumeFormat>(u8_data);
			archive >> storedMinMax_;
			archive >> originalMinMax_;
			archive >> matAlign_;

			archive >> histogram_.minValue;
			archive >> histogram_.maxValue;
			archive >> histogram_.numBins;
			archive >> histogram_.range;
			archive >> histogram_.range_rcp;
			archive >> histogram_.histogram;
		}
		else
		{
			archive << SCU8(IntrinsicType); // or ctype_

			archive << voxelSize_;
			archive << SCU8(volFormat_);
			archive << storedMinMax_;
			archive << originalMinMax_;
			archive << matAlign_;

			archive << histogram_.minValue;
			archive << histogram_.maxValue;
			archive << histogram_.numBins;
			archive << histogram_.range;
			archive << histogram_.range_rcp;
			archive << histogram_.histogram;
		}
	}

	void RenderableComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> flags_;
			archive >> visibleLayerMask_;
			archive >> vuidGeometry_;
			archive >> fadeDistance_;
			archive >> visibleCenter_;
			archive >> visibleRadius_;

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

			archive << flags_;
			archive << visibleLayerMask_;
			archive << vuidGeometry_;
			archive << fadeDistance_;
			archive << visibleCenter_;
			archive << visibleRadius_;

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
			archive >> radius_;
			archive >> length_;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << color_;
			archive << range_;
			archive << radius_;
			archive << length_;
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