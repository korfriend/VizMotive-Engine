#pragma once
#include "CommonInclude.h"
#include "VzEnums.h"
#include "Libs/Math.h"
#include "Utils/ECS.h"

#include <string>
namespace vz { class Archive; namespace ecs { struct EntitySerializer; } }

namespace vz::component
{
	using namespace vz;
	using namespace vz::ecs;
	// ECS components

	struct NameComponent
	{
		std::string name;

		inline void operator=(const std::string& str) { name = str; }
		inline void operator=(std::string&& str) { name = std::move(str); }
		inline bool operator==(const std::string& str) const { return name.compare(str) == 0; }

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct TransformComponent
	{
	private:
		bool isDirty_ = true;
		bool isMatrixAutoUpdate_ = true;
		XMFLOAT3 scale_ = XMFLOAT3(1, 1, 1);
		XMFLOAT4 rotation_ = XMFLOAT4(0, 0, 0, 1);	// this is a quaternion
		XMFLOAT3 position_ = XMFLOAT3(0, 0, 0);
		XMFLOAT4X4 local_ = vz::math::IDENTITY_MATRIX;

		// Non-serialized attributes:

		// The world matrix can be computed from local scale, rotation, translation
		//	- by calling UpdateTransform()
		//	- or by calling SetDirty() and letting the TransformUpdateSystem handle the updating
		XMFLOAT4X4 world_ = vz::math::IDENTITY_MATRIX;

		void setDirty(const bool isDirty) { isDirty_ = isDirty; }
	public:

		bool IsDirty() const { return isDirty_; }
		bool IsMatrixAutoUpdate() const { return isMatrixAutoUpdate_; }

		XMFLOAT3 GetWorldPosition() const;
		XMFLOAT4 GetWorldRotation() const;
		XMFLOAT3 GetWorldScale() const;
		XMFLOAT3 GetWorldForward() const;
		XMFLOAT3 GetWorldUp() const;
		XMFLOAT3 GetWorldRight() const;

		// Local
		XMFLOAT3 GetPosition() const { return position_; };
		XMFLOAT4 GetRotation() const { return rotation_; };
		XMFLOAT3 GetScale() const { return scale_; };

		void SetPosition(const XMFLOAT3& p) { setDirty(true); position_ = p; }
		void SetScale(const XMFLOAT3& s) { setDirty(true); scale_ = s; }
		void SetEulerAngleZXY(const XMFLOAT3& rotAngles); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetEulerAngleZXYInDegree(const XMFLOAT3& rotAngles); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetQuaternion(const XMFLOAT4& q) { setDirty(true); rotation_ = q; }
		void SetMatrix(XMFLOAT4X4 local);

		void UpdateMatrix();

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct HierarchyComponent
	{
		Entity parentID = INVALID_ENTITY;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// resources

	struct MaterialComponent
	{
	public:
		enum class ROption
		{
			DAFAULT = 1 << 0,
			CAST_SHADOW = 1 << 1,	// not yet
			USE_VERTEXCOLORS = 1 << 2,
			SPECULAR_GLOSSINESS_WORKFLOW = 1 << 3,
			OCCLUSION_PRIMARY = 1 << 4,
			OCCLUSION_SECONDARY = 1 << 5,
			DISABLE_RECEIVE_SHADOW = 1 << 6,
			DOUBLE_SIDED = 1 << 7,
			OUTLINE = 1 << 8,
			FORWARD = 1 << 9
		};
		enum class ShaderType : uint32_t
		{
			PHONG = 0,
			PBR,
			COUNT
		};

	private:
		bool isDirty_ = true;
		uint32_t renderOptionFlags_ = CAST_SHADOW;
	public:
		ShaderType shaderType = ShaderType::PHONG;

		inline static const std::vector<std::string> shaderTypeDefines[] = {
			{"PHONG"}, // SHADERTYPE_PBR,
			{"PBR"}, // PBR,
		};
		static_assert((size_t)ShaderType::COUNT == arraysize(shaderTypeDefines), "These values must match!");

		XMFLOAT4 baseColor = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 specularColor = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 emissiveColor = XMFLOAT4(1, 1, 1, 0);
		XMFLOAT4 subsurfaceScattering = XMFLOAT4(1, 1, 1, 0);	// to do
		XMFLOAT4 extinctionColor = XMFLOAT4(0, 0.9f, 1, 1);	// to do

		XMFLOAT4 phongFactors = XMFLOAT4(0.2f, 1, 1, 1);

		enum TEXTURESLOT
		{
			BASECOLORMAP,
			NORMALMAP,
			SURFACEMAP,
			EMISSIVEMAP,
			DISPLACEMENTMAP,
			OCCLUSIONMAP,
			TRANSMISSIONMAP,
			SHEENCOLORMAP,
			SHEENROUGHNESSMAP,
			CLEARCOATMAP,
			CLEARCOATROUGHNESSMAP,
			CLEARCOATNORMALMAP,
			SPECULARMAP,
			ANISOTROPYMAP,
			TRANSPARENCYMAP,
			VOLUMEMAP,

			TEXTURESLOT_COUNT
		};
		struct TextureMap
		{
			std::string name;
			vz::Resource resource;
			uint32_t uvset = 0;
			const vz::graphics::GPUResource* GetGPUResource() const
			{
				if (!resource.IsValid() || !resource.GetTexture().IsValid())
					return nullptr;
				return &resource.GetTexture();
			}

			// Non-serialized attributes:
			float lod_clamp = 0;						// optional, can be used by texture streaming
			int sparse_residencymap_descriptor = -1;	// optional, can be used by texture streaming
			int sparse_feedbackmap_descriptor = -1;		// optional, can be used by texture streaming
		};
		TextureMap textures[TEXTURESLOT_COUNT];

		int customShaderID = -1;

		// Non-serialized attributes:
		uint32_t layerMask = ~0u;
		int sampler_descriptor = -1; // optional

		// User stencil value can be in range [0, 15]
		inline void SetUserStencilRef(uint8_t value)
		{
			assert(value < 16);
			userStencilRef = value & 0x0F;
		}
		uint32_t GetStencilRef() const;

		inline float GetOpacity() const { return baseColor.w; }
		inline float GetEmissiveStrength() const { return emissiveColor.w; }
		inline int GetCustomShaderID() const { return customShaderID; }

		inline bool HasPlanarReflection() const { return shaderType == SHADERTYPE_PBR_PLANARREFLECTION || shaderType == SHADERTYPE_WATER; }

		inline void SetDirty(bool value = true) { if (value) { _flags |= DIRTY; } else { _flags &= ~DIRTY; } }
		inline bool IsDirty() const { return _flags & DIRTY; }

		inline void SetCastShadow(bool value) { SetDirty(); if (value) { _flags |= CAST_SHADOW; } else { _flags &= ~CAST_SHADOW; } }
		inline void SetReceiveShadow(bool value) { SetDirty(); if (value) { _flags &= ~DISABLE_RECEIVE_SHADOW; } else { _flags |= DISABLE_RECEIVE_SHADOW; } }
		inline void SetOcclusionEnabled_Primary(bool value) { SetDirty(); if (value) { _flags |= OCCLUSION_PRIMARY; } else { _flags &= ~OCCLUSION_PRIMARY; } }
		inline void SetOcclusionEnabled_Secondary(bool value) { SetDirty(); if (value) { _flags |= OCCLUSION_SECONDARY; } else { _flags &= ~OCCLUSION_SECONDARY; } }

		inline wi::enums::BLENDMODE GetBlendMode() const { if (userBlendMode == wi::enums::BLENDMODE_OPAQUE && (GetFilterMask() & wi::enums::FILTER_TRANSPARENT)) return wi::enums::BLENDMODE_ALPHA; else return userBlendMode; }
		inline bool IsCastingShadow() const { return _flags & CAST_SHADOW; }
		inline bool IsAlphaTestEnabled() const { return alphaRef <= 1.0f - 1.0f / 256.0f; }
		inline bool IsUsingVertexColors() const { return _flags & USE_VERTEXCOLORS; }
		inline bool IsUsingWind() const { return _flags & USE_WIND; }
		inline bool IsReceiveShadow() const { return (_flags & DISABLE_RECEIVE_SHADOW) == 0; }
		inline bool IsUsingSpecularGlossinessWorkflow() const { return _flags & SPECULAR_GLOSSINESS_WORKFLOW; }
		inline bool IsOcclusionEnabled_Primary() const { return _flags & OCCLUSION_PRIMARY; }
		inline bool IsOcclusionEnabled_Secondary() const { return _flags & OCCLUSION_SECONDARY; }
		inline bool IsCustomShader() const { return customShaderID >= 0; }
		inline bool IsDoubleSided() const { return _flags & DOUBLE_SIDED; }
		inline bool IsOutlineEnabled() const { return _flags & OUTLINE; }
		inline bool IsPreferUncompressedTexturesEnabled() const { return _flags & PREFER_UNCOMPRESSED_TEXTURES; }
		inline bool IsVertexAODisabled() const { return _flags & DISABLE_VERTEXAO; }
		inline bool IsTextureStreamingDisabled() const { return _flags & DISABLE_TEXTURE_STREAMING; }

		inline void SetBaseColor(const XMFLOAT4& value) { SetDirty(); baseColor = value; }
		inline void SetSpecularColor(const XMFLOAT4& value) { SetDirty(); specularColor = value; }
		inline void SetEmissiveColor(const XMFLOAT4& value) { SetDirty(); emissiveColor = value; }
		inline void SetRoughness(float value) { SetDirty(); roughness = value; }
		inline void SetReflectance(float value) { SetDirty(); reflectance = value; }
		inline void SetMetalness(float value) { SetDirty(); metalness = value; }
		inline void SetEmissiveStrength(float value) { SetDirty(); emissiveColor.w = value; }
		inline void SetTransmissionAmount(float value) { SetDirty(); transmission = value; }
		inline void SetCloakAmount(float value) { SetDirty(); cloak = value; }
		inline void SetChromaticAberrationAmount(float value) { SetDirty(); chromatic_aberration = value; }
		inline void SetRefractionAmount(float value) { SetDirty(); refraction = value; }
		inline void SetNormalMapStrength(float value) { SetDirty(); normalMapStrength = value; }
		inline void SetParallaxOcclusionMapping(float value) { SetDirty(); parallaxOcclusionMapping = value; }
		inline void SetDisplacementMapping(float value) { SetDirty(); displacementMapping = value; }
		inline void SetSubsurfaceScatteringColor(XMFLOAT3 value)
		{
			SetDirty();
			subsurfaceScattering.x = value.x;
			subsurfaceScattering.y = value.y;
			subsurfaceScattering.z = value.z;
		}
		inline void SetSubsurfaceScatteringAmount(float value) { SetDirty(); subsurfaceScattering.w = value; }
		inline void SetOpacity(float value) { SetDirty(); baseColor.w = value; }
		inline void SetAlphaRef(float value) { SetDirty();  alphaRef = value; }
		inline void SetUseVertexColors(bool value) { SetDirty(); if (value) { _flags |= USE_VERTEXCOLORS; } else { _flags &= ~USE_VERTEXCOLORS; } }
		inline void SetUseWind(bool value) { SetDirty(); if (value) { _flags |= USE_WIND; } else { _flags &= ~USE_WIND; } }
		inline void SetUseSpecularGlossinessWorkflow(bool value) { SetDirty(); if (value) { _flags |= SPECULAR_GLOSSINESS_WORKFLOW; } else { _flags &= ~SPECULAR_GLOSSINESS_WORKFLOW; } }
		inline void SetSheenColor(const XMFLOAT3& value)
		{
			sheenColor = XMFLOAT4(value.x, value.y, value.z, sheenColor.w);
			SetDirty();
		}
		inline void SetExtinctionColor(const XMFLOAT4& value)
		{
			extinctionColor = XMFLOAT4(value.x, value.y, value.z, value.w);
			SetDirty();
		}
		inline void SetSheenRoughness(float value) { sheenRoughness = value; SetDirty(); }
		inline void SetClearcoatFactor(float value) { clearcoat = value; SetDirty(); }
		inline void SetClearcoatRoughness(float value) { clearcoatRoughness = value; SetDirty(); }
		inline void SetCustomShaderID(int id) { customShaderID = id; }
		inline void DisableCustomShader() { customShaderID = -1; }
		inline void SetDoubleSided(bool value = true) { if (value) { _flags |= DOUBLE_SIDED; } else { _flags &= ~DOUBLE_SIDED; } }
		inline void SetOutlineEnabled(bool value = true) { if (value) { _flags |= OUTLINE; } else { _flags &= ~OUTLINE; } }
		inline void SetPreferUncompressedTexturesEnabled(bool value = true) { if (value) { _flags |= PREFER_UNCOMPRESSED_TEXTURES; } else { _flags &= ~PREFER_UNCOMPRESSED_TEXTURES; } CreateRenderData(true); }
		inline void SetVertexAODisabled(bool value = true) { if (value) { _flags |= DISABLE_VERTEXAO; } else { _flags &= ~DISABLE_VERTEXAO; } }
		inline void SetTextureStreamingDisabled(bool value = true) { if (value) { _flags |= DISABLE_TEXTURE_STREAMING; } else { _flags &= ~DISABLE_TEXTURE_STREAMING; } }

		// The MaterialComponent will be written to ShaderMaterial (a struct that is optimized for GPU use)
		void WriteShaderMaterial(ShaderMaterial* dest) const;
		void WriteShaderTextureSlot(ShaderMaterial* dest, int slot, int descriptor);

		// Retrieve the array of textures from the material
		void WriteTextures(const wi::graphics::GPUResource** dest, int count) const;

		// Returns the bitwise OR of all the wi::enums::FILTER flags applicable to this material
		uint32_t GetFilterMask() const;

		wi::resourcemanager::Flags GetTextureSlotResourceFlags(TEXTURESLOT slot);

		// Create texture resources for GPU
		void CreateRenderData(bool force_recreate = false);

		void Serialize(wi::Archive& archive, wi::ecs::EntitySerializer& seri);
	};

	struct GeometryComponent
	{
		struct Primitive {
			//vertexbuffer gpu handle
			//indexbuffer gpu handle
			//morphbuffer gpu handle
			
			//Aabb aabb; // object-space bounding box
			//UvMap uvmap; // mapping from each glTF UV set to either UV0 or UV1 (8 bytes)
			uint32_t morphTargetOffset;
			std::vector<int> slotIndices;

			enums::PrimitiveType ptype = enums::PrimitiveType::TRIANGLES;
		};

		std::vector<Primitive> parts;

		std::vector<char> cacheVB;
		std::vector<char> cacheIB;
		std::vector<char> cacheMTB;
	public:
		bool isSystem = false;
		//gltfio::FilamentAsset* assetOwner = nullptr; // has ownership
		//filament::Aabb aabb;
		//void Set(const std::vector<VzPrimitive>& primitives);
		//std::vector<VzPrimitive>* Get();
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct TextureComponent
	{
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// scene 

	struct RenderableComponent
	{
		ecs::Entity geometryEntity = ecs::INVALID_ENTITY;
		std::vector<ecs::Entity> miEntities;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
		// internal geometry
		// internal mi
		// internal texture
	};

	struct LightComponent
	{
		enums::LightFlags flags = enums::LightFlags::EMPTY;

		enums::LightType type = enums::LightType::POINT;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CameraComponent
	{
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};
}
