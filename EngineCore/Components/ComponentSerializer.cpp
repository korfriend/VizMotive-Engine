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

			std::string name;
			archive >> name;
			SetName(name);
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_
			
			archive << name_;
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
			archive >> local_;

			timeStampWorldUpdate_ = TimerNow;
			UpdateMatrix();
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << isMatrixAutoUpdate_; // maybe not needed just for dirtiness, but later might come handy if we have more persistent flags
			archive << scale_;
			archive << rotation_;
			archive << position_;
			archive << local_;
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

	void LayeredMaskComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> visibleLayerMask_;
			archive >> stencilLayerMask_;
			archive >> userLayerMask_;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << visibleLayerMask_;
			archive << stencilLayerMask_;
			archive << userLayerMask_;
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
			archive >> saturation_;
			archive >> wireframe_;

			archive >> sheenColor_;
			archive >> subsurfaceScattering_;
			archive >> extinctionColor_;
			archive >> sheenRoughness_;
			archive >> clearcoat_;
			archive >> clearcoatRoughness_;
			archive >> reflectance_;
			archive >> refraction_;
			archive >> normalMapStrength_;
			archive >> parallaxOcclusionMapping_;
			archive >> displacementMapping_;
			archive >> transmission_;
			archive >> alphaRef_;
			archive >> anisotropyStrength_;
			archive >> anisotropyRotation_; 
			archive >> blendWithTerrainHeight_;
			archive >> cloak_;
			archive >> chromaticAberration_;

			archive >> u32_data;
			engineStencilRef_ = static_cast<StencilRef>(u32_data);
			archive >> userStencilRef_;

			// need to check version 
			archive >> u32_data;
			assert(u32_data <= SCU32(TextureSlot::TEXTURESLOT_COUNT));
			for (uint32_t i = 0, n = u32_data; i < n; ++i)
			{
				archive >> vuidTextureComponents_[i];
			}

			archive >> u32_data;
			assert(u32_data <= SCU32(VolumeTextureSlot::VOLUME_TEXTURESLOT_COUNT));
			for (uint32_t i = 0, n = u32_data; i < n; ++i)
			{
				archive >> vuidVolumeTextureComponents_[i];
			}

			archive >> u32_data;
			assert(u32_data <= SCU32(LookupTableSlot::LOOKUPTABLE_COUNT));
			for (uint32_t i = 0, n = u32_data; i < n; ++i)
			{
				archive >> vuidLookupTextureComponents_[i];
			}

			archive >> texMulAdd_;

			archive >> vuidVolumeMapperRenderable_;
			archive >> u32_data; volumemapperVolumeSlot_ = static_cast<VolumeTextureSlot>(u32_data);
			archive >> u32_data; volumemapperLookupSlot_ = static_cast<LookupTableSlot>(u32_data);

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
			archive << saturation_;
			archive << wireframe_;

			archive << sheenColor_;
			archive << subsurfaceScattering_;
			archive << extinctionColor_;
			archive << sheenRoughness_;
			archive << clearcoat_;
			archive << clearcoatRoughness_;
			archive << reflectance_;
			archive << refraction_;
			archive << normalMapStrength_;
			archive << parallaxOcclusionMapping_;
			archive << displacementMapping_;
			archive << transmission_;
			archive << alphaRef_;
			archive << anisotropyStrength_;
			archive << anisotropyRotation_;
			archive << blendWithTerrainHeight_;
			archive << cloak_;
			archive << chromaticAberration_;

			archive << SCU32(engineStencilRef_);
			archive << userStencilRef_;

			uint32_t tex_slot_count = SCU32(TextureSlot::TEXTURESLOT_COUNT);
			archive << tex_slot_count;
			for (uint32_t i = 0; i < tex_slot_count; ++i)
			{
				archive << vuidTextureComponents_[i];
			}
			tex_slot_count = SCU32(VolumeTextureSlot::VOLUME_TEXTURESLOT_COUNT);
			archive << tex_slot_count;
			for (uint32_t i = 0; i < tex_slot_count; ++i)
			{
				archive << vuidVolumeTextureComponents_[i];
			}
			tex_slot_count = SCU32(LookupTableSlot::LOOKUPTABLE_COUNT);
			archive << tex_slot_count;
			for (uint32_t i = 0; i < tex_slot_count; ++i)
			{
				archive << vuidLookupTextureComponents_[i];
			}

			archive << texMulAdd_;

			archive << vuidVolumeMapperRenderable_;
			archive << SCU32(volumemapperVolumeSlot_);
			archive << SCU32(volumemapperLookupSlot_);
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

			archive >> data32t;
			customBuffers_.resize(data32t);
			for (uint32_t i = 0; i < data32t; ++i)
			{
				std::vector<uint8_t>& custom_buffer = customBuffers_[i];
				archive >> custom_buffer;
			}			
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

			archive << (uint32_t)customBuffers_.size();
			for (auto& it : customBuffers_)
			{
				archive << it;
			}
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
			archive >> tableValidBeginEndX_;
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
			archive << tableValidBeginEndX_;
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
			archive >> matVS2OS_;
			archive >> matOS2VS_;
			archive >> matVS2TS_;
			archive >> matTS2VS_;

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
			archive << matVS2OS_;
			archive << matOS2VS_;
			archive << matVS2TS_;
			archive << matTS2VS_;

			archive << histogram_.minValue;
			archive << histogram_.maxValue;
			archive << histogram_.numBins;
			archive << histogram_.range;
			archive << histogram_.range_rcp;
			archive << histogram_.histogram;
		}
	}

	void ColliderComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> flags_;
			archive >> u8_data; shape_ = (Shape)u8_data;
			archive >> radius_;
			archive >> offset_;
			archive >> tail_;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << flags_;
			archive << (uint8_t)shape_;
			archive << radius_;
			archive << offset_;
			archive << tail_;
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
			archive >> u8_data; renderableType_ = static_cast<RenderableType>(u8_data);
			archive >> u8_data; renderableReservedType_ = static_cast<RenderableType>(u8_data);
			archive >> u8_data; engineStencilRef_ = static_cast<StencilRef>(u8_data);
			archive >> userStencilRef_;
			archive >> vuidGeometry_;
			archive >> fadeDistance_;
			archive >> visibleCenter_;
			archive >> visibleRadius_;
			archive >> rimHighlightColor_;
			archive >> rimHighlightFalloff_;
			archive >> clipBox_;
			archive >> clipPlane_;

			archive >> outlineThickness_;
			archive >> outlineColor_;
			archive >> outlineThreshold_;
			archive >> undercutDirection_;
			archive >> undercutColor_;
			archive >> lod_bias_;
			archive >> alphaRef_;

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
			archive << static_cast<uint8_t>(renderableType_);
			archive << static_cast<uint8_t>(renderableReservedType_);
			archive << static_cast<uint8_t>(engineStencilRef_);
			archive << userStencilRef_;
			archive << vuidGeometry_;
			archive << fadeDistance_;
			archive << visibleCenter_;
			archive << visibleRadius_;
			archive << rimHighlightColor_;
			archive << rimHighlightFalloff_;
			archive << clipBox_;
			archive << clipPlane_;

			archive << outlineThickness_;
			archive << outlineColor_;
			archive << outlineThreshold_;
			archive << undercutDirection_;
			archive << undercutColor_;
			archive << lod_bias_;
			archive << alphaRef_;

			archive << vuidMaterials_.size();
			for (size_t i = 0, n = vuidMaterials_.size(); i < n; ++i)
			{
				archive << vuidMaterials_[i];
			}
		}
	}

	void SpriteComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_

			archive >> flags_;
			archive >> vuidSpriteTexture_;

			archive >> position_;
			archive >> rotation_;
			archive >> scale_;
			archive >> opacity_;
			archive >> fade_;
			archive >> uvOffset_;

			archive >> anim_.repeatable;
			archive >> anim_.vel;
			archive >> anim_.rot;
			archive >> anim_.scaleX;
			archive >> anim_.scaleY;
			archive >> anim_.opa;
			archive >> anim_.fad;
			archive >> anim_.movingTexAnim.speedX;
			archive >> anim_.movingTexAnim.speedY;
			archive >> anim_.drawRectAnim.frameRate;
			archive >> anim_.drawRectAnim.frameCount;
			archive >> anim_.drawRectAnim.horizontalFrameCount;
			archive >> anim_.wobbleAnim.amount;
			archive >> anim_.wobbleAnim.speed;
			archive >> anim_.drawRectAnim._currentFrame;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << flags_;
			archive << vuidSpriteTexture_;

			archive << position_;
			archive << rotation_;
			archive << scale_;
			archive << opacity_;
			archive << fade_;
			archive << uvOffset_;

			archive << anim_.repeatable;
			archive << anim_.vel;
			archive << anim_.rot;
			archive << anim_.scaleX;
			archive << anim_.scaleY;
			archive << anim_.opa;
			archive << anim_.fad;
			archive << anim_.movingTexAnim.speedX;
			archive << anim_.movingTexAnim.speedY;
			archive << anim_.drawRectAnim.frameRate;
			archive << anim_.drawRectAnim.frameCount;
			archive << anim_.drawRectAnim.horizontalFrameCount;
			archive << anim_.wobbleAnim.amount;
			archive << anim_.wobbleAnim.speed;
			archive << anim_.drawRectAnim._currentFrame;
		}
	}

	void SpriteFontComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		if (archive.IsReadMode())
		{
			uint8_t u8_data;
			archive >> u8_data;
			assert(IntrinsicType == static_cast<ComponentType>(u8_data));	// or ctype_
			
			uint32_t u32_data;

			archive >> flags_;
			archive >> position_;
			archive >> fontStyle_; 
			std::string textA;
			archive >> textA; SetText(textA);
			archive >> size_;
			archive >> scale_;
			archive >> rotation_;
			archive >> spacing_;
			archive >> u32_data; horizonAlign_ = static_cast<Alignment>(u32_data);
			archive >> u32_data; verticalAlign_ = static_cast<Alignment>(u32_data); ;
			archive >> color_;
			archive >> shadowColor_;
			archive >> wrap_;
			archive >> softness_;
			archive >> bolden_;
			archive >> shadowSoftness_;
			archive >> shadowBolden_;
			archive >> shadowOffset_;
			archive >> hdrScaling_;
			archive >> intensity_;
			archive >> shadowIntensity_;

			archive >> cursor_.position;
			archive >> cursor_.size;

			archive >> anim_.typewriter.characterStart;
			archive >> anim_.typewriter.elapsed;
			archive >> anim_.typewriter.time;
			archive >> anim_.typewriter.looped;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << flags_;
			archive << position_;
			archive << fontStyle_;
			std::string textA = GetTextA();
			archive << textA;
			archive << size_;
			archive << scale_;
			archive << rotation_;
			archive << spacing_;
			archive << (uint32_t)horizonAlign_;
			archive << (uint32_t)verticalAlign_;
			archive << color_;
			archive << shadowColor_;
			archive << wrap_;
			archive << softness_;
			archive << bolden_;
			archive << shadowSoftness_;
			archive << shadowBolden_;
			archive << shadowOffset_;
			archive << hdrScaling_;
			archive << intensity_;
			archive << shadowIntensity_;

			archive << cursor_.position;
			archive << cursor_.size;

			archive << anim_.typewriter.characterStart;
			archive << anim_.typewriter.elapsed;
			archive << anim_.typewriter.time;
			archive << anim_.typewriter.looped;
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
			archive >> outerConeAngle_;
			archive >> innerConeAngle_;
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << color_;
			archive << range_;
			archive << radius_;
			archive << length_;
			archive << outerConeAngle_;
			archive << innerConeAngle_;
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

			archive >> fx_;
			archive >> fy_;
			archive >> sc_;
			archive >> cx_;
			archive >> cy_;

			archive >> focalLength_;
			archive >> apertureSize_;
			archive >> apertureShape_;
			archive >> orthoVerticalSize_;
			archive >> clipBox_;
			archive >> clipPlane_;

			float cur_width = width_;
			float cur_height = height_;
			archive >> width_;
			archive >> height_;
			{
				// correction for image ratio
				float cur_ratio = cur_width / cur_height;
				float loaded_ratio = width_ / height_;

				width_ = cur_ratio * height_;
			}

			archive >> flags_;

			archive >> exposure_;
			archive >> brightness_;
			archive >> contrast_;
			archive >> saturation_;
			archive >> eyeAdaptionEnabled_;
			archive >> bloomEnabled_;
			archive >> hdrCalibration_;
			uint32_t u32_data;
			archive >> u32_data; dvrLookup_ = static_cast<MaterialComponent::LookupTableSlot>(u32_data);

			isDirty_ = true;
			SetWorldLookAtFromHierarchyTransforms();
		}
		else
		{
			archive << static_cast<uint8_t>(IntrinsicType); // or ctype_

			archive << zNearP_;
			archive << zFarP_;
			archive << fovY_;

			archive << fx_;
			archive << fy_;
			archive << sc_;
			archive << cx_;
			archive << cy_;

			archive << focalLength_;
			archive << apertureSize_;
			archive << apertureShape_;
			archive << orthoVerticalSize_;
			archive << clipBox_;
			archive << clipPlane_;

			archive << width_;
			archive << height_;
			archive << flags_;

			archive << exposure_;
			archive << brightness_;
			archive << contrast_;
			archive << saturation_;
			archive << eyeAdaptionEnabled_;
			archive << bloomEnabled_;
			archive << hdrCalibration_;
			archive << static_cast<uint32_t>(dvrLookup_);
		}
	}

	void SlicerComponent::Serialize(vz::Archive& archive, const uint64_t version)
	{
		CameraComponent::Serialize(archive, version);
		if (archive.IsReadMode())
		{
			archive >> thickness_;
			archive >> outlineThickness_;

			archive >> horizontalCurveControls_;
			archive >> curveInterpolationInterval_;
			archive >> curvedPlaneHeight_;
			archive >> isReverseSide_;

			archive >> curvedSlicerUp_;
		}
		else
		{
			archive << thickness_;
			archive << outlineThickness_;

			archive << horizontalCurveControls_;
			archive << curveInterpolationInterval_;
			archive << curvedPlaneHeight_;
			archive << isReverseSide_;

			archive << curvedSlicerUp_;
		}
	}
}