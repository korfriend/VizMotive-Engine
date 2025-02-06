#pragma once
#include "Libs/Math.h"
#include "Libs/Geometrics.h"

#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <chrono>
#include <mutex>

#ifdef _WIN32
#define CORE_EXPORT __declspec(dllexport)
#else
#define CORE_EXPORT __attribute__((visibility("default")))
#endif

#define PLUGIN_EXPORT CORE_EXPORT

using GpuHandler = uint32_t;
using Entity = uint32_t;
using VUID = uint64_t;
inline constexpr Entity INVALID_ENTITY = 0;
inline constexpr VUID INVALID_VUID = 0;
using TimeStamp = std::chrono::high_resolution_clock::time_point;
#define TimeDurationCount(A, B) std::chrono::duration_cast<std::chrono::duration<double>>(A - B).count()
#define TimerNow std::chrono::high_resolution_clock::now()
#define TimerMin {} // indicating 1970/1/1 (00:00:00 UTC), DO NOT USE 'std::chrono::high_resolution_clock::time_point::min()'
#define SETVISIBLEMASK(MASK, LAYERBITS, MASKBITS) MASK = (MASK & ~LAYERBITS) | (MASKBITS & LAYERBITS);

namespace vz
{
	inline static const std::string COMPONENT_INTERFACE_VERSION = "VZ::20241118";
	inline static std::string stringEntity(Entity entity) { return "(" + std::to_string(entity) + ")"; }
	CORE_EXPORT std::string GetComponentVersion();

	class Archive;
	struct GScene;
	struct Resource;

	enum class RenderableFilterFlags
	{
		RENDERABLE_MESH_OPAQUE = 1 << 0,
		RENDERABLE_MESH_TRANSPARENT = 1 << 1,
		RENDERABLE_MESH_NAVIGATION = 1 << 3,
		RENDERABLE_COLLIDER = 1 << 4,
		RENDERABLE_VOLUME_DVR = 1 << 5,
		// Include everything:
		RENDERABLE_ALL = ~0,
	};

	struct CORE_EXPORT Scene
	{
	public:
		static Scene* GetScene(const Entity entity);
		static Scene* GetFirstSceneByName(const std::string& name);
		static Scene* GetSceneIncludingEntity(const Entity entity);
		static Scene* CreateScene(const std::string& name, const Entity entity = 0);
		static void RemoveEntityForScenes(const Entity entity);	// calling when the entity is removed
		static bool DestroyScene(const Entity entity);
		static void DestroyAll();
		static uint32_t GetIndex(const std::vector<Entity>& entities, const Entity targetEntity)
		{
			std::vector<Entity>& _entities = (std::vector<Entity>&)entities;
			for (uint32_t i = 0, n = (uint32_t)entities.size(); i < n; ++i)
			{
				if (_entities[i] == targetEntity)
				{
					return i;
				}
			}
			return ~0u;
		}

	protected:
		std::string name_;

		// Scene lights (Skybox or Weather something...)
		XMFLOAT3 ambient_ = XMFLOAT3(0.25f, 0.25f, 0.25f);

		std::string skyMapName_;	// resourcemanager's key
		std::string colorGradingMapName_; // resourcemanager's key

		// Instead of Entity, VUID is stored by serialization
		//	the index is same to the streaming index
		std::vector<Entity> renderables_;
		std::vector<Entity> lights_;

		// TODO
		// camera for reflection 

		// -----------------------------------------
		// Non-serialized attributes:
		//	Note: transform states are based on those streams
		std::unordered_map<Entity, size_t> lookupRenderables_; // each entity has also TransformComponent and HierarchyComponent
		std::unordered_map<Entity, size_t> lookupLights_;
		std::vector<Entity> materials_;
		std::vector<Entity> geometries_;

		// AABB culling streams:
		std::vector<geometrics::AABB> aabbRenderables_;
		std::vector<geometrics::AABB> aabbLights_;
		//std::vector<geometrics::AABB> aabbProbes_;
		//std::vector<geometrics::AABB> aabbDecals_;

		// Separate stream of world matrices:
		std::vector<XMFLOAT4X4> matrixRenderables_;
		std::vector<XMFLOAT4X4> matrixRenderablesPrev_;

		std::shared_ptr<Resource> skyMap_;
		std::shared_ptr<Resource> colorGradingMap_;

		geometrics::AABB aabb_;

		Entity entity_ = INVALID_ENTITY;
		TimeStamp recentUpdateTime_ = TimerMin;	// world update time
		TimeStamp timeStampSetter_ = TimerMin;
		
		bool isDirty_ = true;

		// instant parameters during render-process
		float dt_ = 0.f;

		GScene* handlerScene_ = nullptr;

		inline size_t scanGeometryEntities() noexcept;
		inline size_t scanMaterialEntities() noexcept;

	public:
		Scene(const Entity entity, const std::string& name);
		~Scene();

		uint32_t mostImportantLightIndex = ~0u;
		const void* GetTextureSkyMap() const;			// return the pointer of graphics::Texture
		const void* GetTextureGradientMap() const;	// return the pointer of graphics::Texture

		void SetDirty() { isDirty_ = true; }
		bool IsDirty() const { return isDirty_; }

		inline const std::string GetSceneName() const { return name_; }
		inline const Entity GetSceneEntity() const { return entity_; }
		inline const GScene* GetGSceneHandle() const { return handlerScene_; }

		inline void SetAmbient(const XMFLOAT3& ambient) { ambient_ = ambient; }
		inline XMFLOAT3 GetAmbient() const { return ambient_; }

		inline bool LoadIBL(const std::string& filename); // to skyMap_

		inline void Update(const float dt);

		inline void Clear();

		inline void AddEntity(const Entity entity);

		/**
		 * Adds a list of entities to the Scene.
		 *
		 * @param entities Array containing entities to add to the scene.
		 * @param count Size of the entity array.
		 */
		inline void AddEntities(const std::vector<Entity>& entities);

		/**
		 * Removes the Renderable from the Scene.
		 *
		 * @param entity The Entity to remove from the Scene. If the specified
		 *                   \p entity doesn't exist, this call is ignored.
		 */
		inline void Remove(const Entity entity);

		/**
		 * Removes a list of entities to the Scene.
		 *
		 * This is equivalent to calling remove in a loop.
		 * If any of the specified entities do not exist in the scene, they are skipped.
		 *
		 * @param entities Array containing entities to remove from the scene.
		 * @param count Size of the entity array.
		 */
		inline void RemoveEntities(const std::vector<Entity>& entities);

		/**
		 * Returns the total number of Entities in the Scene, whether alive or not.
		 * @return Total number of Entities in the Scene.
		 */
		inline size_t GetEntityCount() const noexcept { return renderables_.size() + lights_.size(); }

		/**
		 * Returns the number of active (alive) Renderable objects in the Scene.
		 *
		 * @return The number of active (alive) Renderable objects in the Scene.
		 */
		inline size_t GetRenderableCount() const noexcept { return renderables_.size(); }

		/**
		 * Returns the number of active (alive) Light objects in the Scene.
		 *
		 * @return The number of active (alive) Light objects in the Scene.
		 */
		inline size_t GetLightCount() const noexcept { return lights_.size(); }

		inline const std::vector<Entity>& GetRenderableEntities() const noexcept { return renderables_; }
		inline const std::vector<Entity>& GetLightEntities() const noexcept { return lights_; }

		// requires scanning process
		inline std::vector<Entity> GetGeometryEntities() const noexcept { return geometries_; }
		inline std::vector<Entity> GetMaterialEntities() const noexcept { return materials_; }

		inline const geometrics::AABB& GetAABB() const { return aabb_; }

		/**
		 * Returns true if the given entity is in the Scene.
		 *
		 * @return Whether the given entity is in the Scene.
		 */
		inline bool HasEntity(const Entity entity) const noexcept;

		inline size_t GetEntities(std::vector<Entity>& entities) const
		{
			entities = renderables_;
			entities.insert(entities.end(), lights_.begin(), lights_.end());
			return entities.size();
		}

		//----------- stream states -------------
		inline const std::vector<XMFLOAT4X4>& GetRenderableWorldMatrices() const { return matrixRenderables_; }
		inline const std::vector<XMFLOAT4X4>& GetRenderableWorldMatricesPrev() const { return matrixRenderablesPrev_; }


		struct RayIntersectionResult
		{
			Entity entity = INVALID_ENTITY;
			XMFLOAT3 position = XMFLOAT3(0, 0, 0);
			XMFLOAT3 normal = XMFLOAT3(0, 0, 0);
			XMFLOAT4 uv = XMFLOAT4(0, 0, 0, 0);
			XMFLOAT3 velocity = XMFLOAT3(0, 0, 0);
			float distance = std::numeric_limits<float>::max();
			int subsetIndex = -1;
			int vertexID0 = -1;
			int vertexID1 = -1;
			int vertexID2 = -1;
			XMFLOAT2 bary = XMFLOAT2(0, 0);
			XMFLOAT4X4 orientation = math::IDENTITY_MATRIX;

			constexpr bool operator==(const RayIntersectionResult& other) const
			{
				return entity == other.entity;
			}
		};
		// Given a ray, finds the closest intersection point against all mesh instances or collliders
		//	ray				:	the incoming ray that will be traced
		//	filterMask		:	filter based on type
		//	layerMask		:	filter based on layer
		//	lod				:	specify min level of detail for meshes
		RayIntersectionResult Intersects(const geometrics::Ray& ray, 
			uint32_t filterMask = SCU32(RenderableFilterFlags::RENDERABLE_MESH_OPAQUE), 
			uint32_t layerMask = ~0, uint32_t lod = 0) const;


		/**
		 * Read/write scene components (renderables and lights), make sure their VUID-based components are serialized first
		 */
		inline void Serialize(vz::Archive& archive);
	};

	enum class ComponentType : uint8_t
	{
		UNDEFINED = 0,
		NAME,
		TRANSFORM,
		HIERARCHY,
		MATERIAL,
		GEOMETRY,
		RENDERABLE,
		TEXTURE,
		VOLUMETEXTURE,
		LIGHT,
		CAMERA,
	};

	struct CORE_EXPORT ComponentBase
	{
	protected:
		// global serialized attributes
		ComponentType cType_ = ComponentType::UNDEFINED;
		VUID vuid_ = INVALID_VUID;

		// non-serialized attributes
		TimeStamp timeStampSetter_ = TimerMin;
		Entity entity_ = INVALID_ENTITY;
		//std::atomic_bool isLocked_ = {};

	public:
		ComponentBase() {};
		ComponentBase(const ComponentType compType, const Entity entity, const VUID vuid);
		ComponentType GetComponentType() const { return cType_; }
		TimeStamp GetTimeStamp() const { return timeStampSetter_; }
		Entity GetEntity() const { return entity_; }
		VUID GetVUID() const { return vuid_; }
		//bool IsLocked() const { return isLocked_.load(); }
		//void TryLock() { isLocked_ = { true }; }

		virtual void ResetRefComponents(const VUID vuidRef) {}
		virtual void Serialize(vz::Archive& archive, const uint64_t version) = 0;
	};

	struct CORE_EXPORT NameComponent : ComponentBase
	{
	protected:
		std::string name_;
	public:
		NameComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::NAME, entity, vuid) {}

		inline void SetName(const std::string& name);
		inline std::string GetName() const { return name_; }

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::NAME;
	};

	struct CORE_EXPORT TransformComponent : ComponentBase
	{
	private:
		bool isMatrixAutoUpdate_ = true;
		XMFLOAT3 scale_ = XMFLOAT3(1, 1, 1);
		XMFLOAT4 rotation_ = XMFLOAT4(0, 0, 0, 1);	// this is a quaternion
		XMFLOAT3 position_ = XMFLOAT3(0, 0, 0);
		XMFLOAT4X4 local_ = math::IDENTITY_MATRIX;
		
		// Non-serialized attributes:
		bool isDirty_ = true; // local check
		// The local matrix can be computed from local scale, rotation, translation 
		//	- by calling UpdateMatrix()
		//	- or by isDirty_ := false and letting the TransformUpdateSystem handle the updating

		// check timeStampWorldUpdate_ and global timeStamp
		TimeStamp timeStampWorldUpdate_ = TimerMin;
		XMFLOAT4X4 world_ = math::IDENTITY_MATRIX;

	public:
		TransformComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::TRANSFORM, entity, vuid) {}

		inline bool IsDirty() const { return isDirty_; }
		inline bool IsMatrixAutoUpdate() const { return isMatrixAutoUpdate_; }
		inline void SetMatrixAutoUpdate(const bool enable) { isMatrixAutoUpdate_ = enable; timeStampSetter_ = TimerNow; }

		// recommend checking IsDirtyWorldMatrix with scene's timeStampWorldUpdate
		inline const XMFLOAT3 GetWorldPosition() const;
		inline const XMFLOAT4 GetWorldRotation() const;
		inline const XMFLOAT3 GetWorldScale() const;
		inline const XMFLOAT3 GetWorldForward() const; // z-axis
		inline const XMFLOAT3 GetWorldUp() const;
		inline const XMFLOAT3 GetWorldRight() const;
		inline const XMFLOAT4X4& GetWorldMatrix() const { return world_; };

		// Local
		inline const XMFLOAT3& GetPosition() const { return position_; };
		inline const XMFLOAT4& GetRotation() const { return rotation_; };
		inline const XMFLOAT3& GetScale() const { return scale_; };
		inline const XMFLOAT4X4& GetLocalMatrix() const { return local_; };

		inline void SetPosition(const XMFLOAT3& p) { isDirty_ = true; position_ = p; timeStampSetter_ = TimerNow; }
		inline void SetScale(const XMFLOAT3& s) { isDirty_ = true; scale_ = s; timeStampSetter_ = TimerNow; }
		inline void SetEulerAngleZXY(const XMFLOAT3& rotAngles); // ROLL->PITCH->YAW (mainly used CG-convention) 
		inline void SetEulerAngleZXYInDegree(const XMFLOAT3& rotAngles); // ROLL->PITCH->YAW (mainly used CG-convention) 
		inline void SetQuaternion(const XMFLOAT4& q) { isDirty_ = true; rotation_ = q; timeStampSetter_ = TimerNow; }
		inline void SetMatrix(const XMFLOAT4X4& local);
			
		inline void SetWorldMatrix(const XMFLOAT4X4& world) { world_ = world; timeStampWorldUpdate_ = TimerNow; };

		inline void UpdateMatrix();	// local matrix
		inline void UpdateWorldMatrix(); // call UpdateMatrix() if necessary
		inline bool IsDirtyWorldMatrix(const TimeStamp timeStampRecentWorldUpdate) { return TimeDurationCount(timeStampRecentWorldUpdate, timeStampWorldUpdate_) <= 0; }

		inline void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::TRANSFORM;
	};

	struct CORE_EXPORT HierarchyComponent : ComponentBase
	{
	protected:
		VUID vuidParentHierarchy_ = INVALID_VUID;
		// this is supposed to be automatically updated when
		//	1. adding or removing a child or
		//	2. changing its parent
		std::unordered_set<VUID> children_;	

		// Non-serialized attributes
		std::vector<VUID> childrenCache_;
		inline void updateChildren();

	public:
		HierarchyComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::HIERARCHY, entity, vuid) {}

		inline void SetParent(const VUID vuidParent);
		inline void SetParent(const Entity entityParent);
		inline VUID GetParent() const;
		inline Entity GetParentEntity() const;

		inline void AddChild(const VUID vuidChild);
		inline void RemoveChild(const VUID vuidChild);
		inline const std::vector<VUID>& GetChildren() { if (children_.size() != childrenCache_.size()) updateChildren(); return childrenCache_; }
		
		void ResetRefComponents(const VUID vuidRef) override;
		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::HIERARCHY;
	};

#define FLAG_SETTER(FLAG, FLAG_ENUM) enabled ? FLAG |= SCU32(FLAG_ENUM) : FLAG &= ~SCU32(FLAG_ENUM);

	// resources
	struct CORE_EXPORT MaterialComponent : ComponentBase
	{
	public:
		enum class RenderFlags : uint32_t
		{
			DAFAULT = 1 << 0, // same as FORWARD PHONG
			USE_VERTEXCOLORS = 1 << 1, // Forced OPAQUENESS 
			DOUBLE_SIDED = 1 << 2,
			OUTLINE = 1 << 3,
			FORWARD = 1 << 4, // "not forward" refers to "deferred"
			TRANSPARENCY = 1 << 5,
			TESSELATION = 1 << 6,
			ALPHA_TEST = 1 << 7,
			WETMAP = 1 << 8,
			CAST_SHADOW = 1 << 9,
			RECEIVE_SHADOW = 1 << 10,
			VERTEXAO = 1 << 11,
			GAUSSIAN_SPLATTING = 1 << 12
		};
		enum class ShaderType : uint32_t
		{
			PHONG = 0,
			PBR,
			UNLIT,

			//WATER,

			COUNT	// UPDATE ShaderInterop.h's SHADERTYPE_BIN_COUNT when modifying ShaderType elements
		};
		enum class DirectVolumeShaderType : uint32_t
		{
			DEFAULT = 0,
			//MULTI_OTF_DEFAULT,
			//ALPHA_MODULATION,
			//MULTI_OTF_ALPHA_MODULATION,
			COUNT
		};
		enum class TextureSlot : uint32_t
		{
			BASECOLORMAP = 0,
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

			TEXTURESLOT_COUNT
		};
		enum class LookupTableSlot : uint32_t
		{
			LOOKUP_COLOR,
			LOOKUP_OTF,
			LOOKUP_WINDOWING,

			LOOKUPTABLE_COUNT
		};
		enum class VolumeTextureSlot : uint32_t
		{
			VOLUME_MAIN_MAP, // this is used for volume rendering
			VOLUME_SEMANTIC_MAP,
			VOLUME_SCULPT_MAP,

			VOLUME_TEXTURESLOT_COUNT
		};
		enum class BlendMode : uint32_t
		{
			BLENDMODE_OPAQUE = 0,
			BLENDMODE_ALPHA,
			BLENDMODE_PREMULTIPLIED,
			BLENDMODE_ADDITIVE,
			BLENDMODE_MULTIPLY,
			BLENDMODE_COUNT
		};

		// engine stencil reference values. These can be in range of [0, 15].
		enum class StencilRef : uint8_t
		{
			STENCILREF_EMPTY = 0,
			STENCILREF_DEFAULT = 1,
			STENCILREF_CUSTOMSHADER = 2,
			STENCILREF_OUTLINE = 3,
			STENCILREF_CUSTOMSHADER_OUTLINE = 4,
			STENCILREF_LAST = 15
		};
		// There are two different kinds of stencil refs:
		//	ENGINE	: managed by the engine systems (STENCILREF enum values between 0-15)
		//	USER	: managed by the user (raw numbers between 0-15)
		enum class StencilRefMask : uint8_t
		{
			STENCILREF_MASK_ENGINE = 0x0F,
			STENCILREF_MASK_USER = 0xF0,
			STENCILREF_MASK_ALL = STENCILREF_MASK_ENGINE | STENCILREF_MASK_USER,
		};

	protected:
		uint32_t flags_ = (uint32_t)RenderFlags::FORWARD;
		ShaderType shaderType_ = ShaderType::PHONG;
		BlendMode blendMode_ = BlendMode::BLENDMODE_OPAQUE;
		StencilRef engineStencilRef_ = StencilRef::STENCILREF_DEFAULT;
		DirectVolumeShaderType dvrShaderType_ = DirectVolumeShaderType::DEFAULT;

		float alphaRef_ = 1.f;
		XMFLOAT4 baseColor_ = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 specularColor_ = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 emissiveColor_ = XMFLOAT4(1, 1, 1, 0);

		XMFLOAT4 phongFactors_ = XMFLOAT4(0.2f, 1, 1, 1);	// only used for ShaderType::PHONG
		float metalness_ = 0.f; // only used for ShaderType::PBR
		float roughness_ = 0.f; // only used for ShaderType::PBR
		float saturate_ = 1.f;

		VUID textureComponents_[SCU32(TextureSlot::TEXTURESLOT_COUNT)] = {};
		VUID volumeComponents_[SCU32(VolumeTextureSlot::VOLUME_TEXTURESLOT_COUNT)] = {};
		VUID lookupComponents_[SCU32(LookupTableSlot::LOOKUPTABLE_COUNT)] = {};

		XMFLOAT4 texMulAdd_ = XMFLOAT4(1, 1, 0, 0);

		// Non-serialized Attributes:
		bool isDirty_ = true;
	public:
		MaterialComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::MATERIAL, entity, vuid) {}

		inline const float GetOpacity() const { return baseColor_.w; }
		inline const XMFLOAT4& GetBaseColor() const { return baseColor_; }	// w is opacity
		inline const XMFLOAT4& GetSpecularColor() const { return specularColor_; }
		inline const XMFLOAT4& GetEmissiveColor() const { return emissiveColor_; }	// w is emissive strength

		inline void SetAlphaRef(const float alphaRef) { alphaRef_ = alphaRef; }
		inline void SetSaturate(const float saturate) { saturate_ = saturate; }
		inline void SetBaseColor(const XMFLOAT4& baseColor) { baseColor_ = baseColor; isDirty_ = true; }
		inline void SetSpecularColor(const XMFLOAT4& specularColor) { specularColor_ = specularColor; isDirty_ = true; }
		inline void SetEmissiveColor(const XMFLOAT4& emissiveColor) { emissiveColor_ = emissiveColor; isDirty_ = true; }
		inline void SetMatalness(const float metalness) { metalness_ = metalness; isDirty_ = true; }
		inline void SetRoughness(const float roughness) { roughness_ = roughness; isDirty_ = true; }
		inline void EnableWetmap(const bool enabled) { FLAG_SETTER(flags_, RenderFlags::WETMAP) isDirty_ = true; }
		inline void SetCastShadow(bool enabled) { FLAG_SETTER(flags_, RenderFlags::CAST_SHADOW) isDirty_ = true; }
		inline void SetReceiveShadow(bool enabled) { FLAG_SETTER(flags_, RenderFlags::RECEIVE_SHADOW) isDirty_ = true; }
		inline void SetShaderType(ShaderType shaderType) { shaderType_ = shaderType; }
		inline void SetDoubleSided(bool enabled) { FLAG_SETTER(flags_, RenderFlags::DOUBLE_SIDED) isDirty_ = true; }
		inline void SetGaussianSplatting(bool enabled) { FLAG_SETTER(flags_, RenderFlags::GAUSSIAN_SPLATTING) isDirty_ = true; }

		inline void SetTexture(const Entity textureEntity, const TextureSlot textureSlot);
		inline void SetVolumeTexture(const Entity volumetextureEntity, const VolumeTextureSlot volumetextureSlot);
		inline void SetLookupTable(const Entity lookuptextureEntity, const LookupTableSlot lookuptextureSlot);

		inline bool IsDirty() const { return isDirty_; }
		inline void SetDirty(const bool dirty) { isDirty_ = dirty; }
		inline bool IsOutlineEnabled() const { return flags_ & SCU32(RenderFlags::OUTLINE); }
		inline bool IsDoubleSided() const { return flags_ & SCU32(RenderFlags::DOUBLE_SIDED); }
		inline bool IsTesellated() const { return flags_ & SCU32(RenderFlags::TESSELATION); }
		inline bool IsAlphaTestEnabled() const { return flags_ & SCU32(RenderFlags::ALPHA_TEST); }
		inline bool IsWetmapEnabled() const { return flags_ & SCU32(RenderFlags::WETMAP); }
		inline bool IsCastShadow() const { return flags_ & SCU32(RenderFlags::CAST_SHADOW); }
		inline bool IsReceiveShadow() const { return flags_ & SCU32(RenderFlags::RECEIVE_SHADOW); }
		inline bool IsVertexAOEnabled() const { return flags_ & SCU32(RenderFlags::VERTEXAO); }
		inline bool IsGaussianSplattingEnabled() const { return flags_ & SCU32(RenderFlags::GAUSSIAN_SPLATTING); }

		inline uint32_t GetRenderFlags() const { return flags_; }

		inline StencilRef GetStencilRef() const { return engineStencilRef_; }

		inline float GetAlphaRef() const { return alphaRef_; }
		inline float GetSaturate() const { return saturate_; }
		inline float GetMatalness() const { return metalness_; }
		inline float GetRoughness() const { return roughness_; }
		inline BlendMode GetBlendMode() const { return blendMode_; }
		inline VUID GetTextureVUID(const TextureSlot slot) const { return textureComponents_[SCU32(slot)]; }
		inline VUID GetVolumeTextureVUID(const VolumeTextureSlot slot) const { return volumeComponents_[SCU32(slot)]; }
		inline VUID GetLookupTableVUID(const LookupTableSlot slot) const { return lookupComponents_[SCU32(slot)]; }
		inline const XMFLOAT4 GetTexMulAdd() const { return texMulAdd_; }
		inline ShaderType GetShaderType() const { return shaderType_; }

		virtual void UpdateAssociatedTextures() = 0;

		void ResetRefComponents(const VUID vuidRef) override;
		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::MATERIAL;
	};

	struct GGeometryComponent;
	struct CORE_EXPORT GeometryComponent : ComponentBase
	{
	public:
		enum class PrimitiveType : uint8_t {
			// don't change the enums values (made to match GL)
			POINTS = 0,    //!< points
			LINES = 1,    //!< lines
			LINE_STRIP = 2,    //!< line strip
			TRIANGLES = 3,    //!< triangles
			TRIANGLE_STRIP = 4,     //!< triangle strip
		};
		enum class NormalComputeMethod : uint8_t {
			COMPUTE_NORMALS_HARD,		// hard face normals, can result in additional vertices generated
			COMPUTE_NORMALS_SMOOTH,		// smooth per vertex normals, this can remove/simplify geometry, but slow
			COMPUTE_NORMALS_SMOOTH_FAST	// average normals, vertex count will be unchanged, fast
		};
		enum class BufferDefinition {
			POSITION = 0,
			NORMAL,
			TANGENT,
			UVSET0,
			UVSET1,
			COLOR,
			INDICES,

			COUNT
		};

		struct SH {
			XMFLOAT3 dcSHs[16];
		};
		struct Primitive {
		private:
			std::vector<XMFLOAT3> vertexPositions_;
			std::vector<XMFLOAT3> vertexNormals_;
			std::vector<XMFLOAT4> vertexTangents_;
			std::vector<XMFLOAT2> vertexUVset0_;
			std::vector<XMFLOAT2> vertexUVset1_;
			std::vector<uint32_t> vertexColors_;
			std::vector<uint32_t> indexPrimitives_;
			// --- Gaussian Splatting ---
			std::vector<SH> vertexSHs_;	// vertex spherical harmonics
			std::vector<XMFLOAT4> vertexScale_Opacities_;	// vertex spherical harmonics
			std::vector<XMFLOAT4> vertexQuaterions_;	// vertex spherical harmonics

			PrimitiveType ptype_ = PrimitiveType::TRIANGLES;

			// Non-serialized Attributes:
			geometrics::AABB aabb_;
			XMFLOAT2 uvRangeMin_ = XMFLOAT2(0, 0);
			XMFLOAT2 uvRangeMax_ = XMFLOAT2(1, 1);
			size_t uvStride_ = 0;
			bool useFullPrecisionUV_ = false;
			std::shared_ptr<void> bufferHandle_;	// 'void' refers to GGeometryComponent::GPrimBuffers
			Entity recentBelongingGeometry_ = INVALID_ENTITY;

			// BVH
			std::vector<geometrics::AABB> bvhLeafAabbs_;
			geometrics::BVH bvh_;

			// OpenMesh-based data structures for acceleration / editing

			//std::shared_ptr<Resource> internalBlock_;

			void updateGpuEssentials(); // supposed to be called in GeometryComponent

			// CPU-side BVH acceleration structure
			//	true: BVH will be built immediately if it doesn't exist yet
			//	false: BVH will be deleted immediately if it exists
			void updateBVH(const bool enabled);

		public:
			mutable bool autoUpdateRenderData = true;

			inline void MoveFrom(Primitive&& primitive) { 
				*this = std::move(primitive); }
			inline void MoveTo(Primitive& primitive) {
				primitive = std::move(*this);
				*this = Primitive();
			}
			inline const geometrics::AABB& GetAABB() const { return aabb_; }
			inline geometrics::Sphere GetBoundingSphere() const {
				geometrics::Sphere sphere;
				sphere.center = aabb_.getCenter();
				sphere.radius = aabb_.getRadius();
				return sphere;
			}
			inline PrimitiveType GetPrimitiveType() const { return ptype_; }
			inline bool IsValid() const { return vertexPositions_.size() > 0 && aabb_.IsValid(); }
			inline void SetAABB(const geometrics::AABB& aabb) { aabb_ = aabb; }
			inline void SetPrimitiveType(const PrimitiveType ptype) { ptype_ = ptype; }
			inline bool IsValidBVH() const { return bvh_.IsValid(); };
			inline const geometrics::BVH& GetBVH() const { return bvh_; };
			inline const std::vector<geometrics::AABB>& GetBVHLeafAABBs() const { return bvhLeafAabbs_; };

			// ----- Getters -----
			inline const std::vector<XMFLOAT3>& GetVtxPositions() const { return vertexPositions_; }
			inline const std::vector<uint32_t>& GetIdxPrimives() const { return indexPrimitives_; }
			inline const std::vector<XMFLOAT3>& GetVtxNormals() const { return vertexNormals_; }
			inline const std::vector<XMFLOAT4>& GetVtxTangents() const { return vertexTangents_; }
			inline const std::vector<XMFLOAT2>& GetVtxUVSet0() const { return vertexUVset0_; }
			inline const std::vector<XMFLOAT2>& GetVtxUVSet1() const { return vertexUVset1_; }
			inline const std::vector<uint32_t>& GetVtxColors() const { return vertexColors_; }
			inline std::vector<XMFLOAT3>& GetMutableVtxPositions() { return vertexPositions_; }
			inline std::vector<uint32_t>& GetMutableIdxPrimives() { return indexPrimitives_; }
			inline std::vector<XMFLOAT3>& GetMutableVtxNormals() { return vertexNormals_; }
			inline std::vector<XMFLOAT4>& GetMutableVtxTangents() { return vertexTangents_; }
			inline std::vector<XMFLOAT2>& GetMutableVtxUVSet0() { return vertexUVset0_; }
			inline std::vector<XMFLOAT2>& GetMutableVtxUVSet1() { return vertexUVset1_; }
			inline std::vector<uint32_t>& GetMutableVtxColors() { return vertexColors_; }

			inline std::vector<SH>& GetMutableVtxSHs() { return vertexSHs_; }	// vertex spherical harmonics
			inline std::vector<XMFLOAT4>& GetMutableVtxScaleOpacities() { return vertexScale_Opacities_; }	// vertex spherical harmonics
			inline std::vector<XMFLOAT4>& GetMutableVtxQuaternions() { return vertexQuaterions_; }	// vertex spherical harmonics


			inline size_t GetNumVertices() const { return vertexPositions_.size(); }
			inline size_t GetNumIndices() const { return indexPrimitives_.size(); }

			inline const XMFLOAT2& GetUVRangeMin() const { return uvRangeMin_; }
			inline const XMFLOAT2& GetUVRangeMax() const { return uvRangeMax_; }

			#define PRIM_SETTER(A, B) A##_ = onlyMoveOwnership ? std::move(A) : A;
			// move or copy
			// note: if onlyMoveOwnership is true, input std::vector will be invalid!
			//	carefully use onlyMoveOwnership(true) to avoid the ABI issue
			void SetVtxPositions(std::vector<XMFLOAT3>& vertexPositions, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexPositions, SCU32(BufferDefinition::POSITION)) }
			void SetVtxNormals(std::vector<XMFLOAT3>& vertexNormals, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexNormals, SCU32(BufferDefinition::NORMAL)) }
			void SetVtxTangents(std::vector<XMFLOAT4>& vertexTangents, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexTangents, SCU32(BufferDefinition::TANGENT)) }
			void SetVtxUVSet0(std::vector<XMFLOAT2>& vertexUVset0, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexUVset0, SCU32(BufferDefinition::UVSET0)) }
			void SetVtxUVSet1(std::vector<XMFLOAT2>& vertexUVset1, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexUVset1, SCU32(BufferDefinition::UVSET1)) }
			void SetVtxColors(std::vector<uint32_t>& vertexColors, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexColors, SCU32(BufferDefinition::COLOR)) }
			void SetIdxPrimives(std::vector<uint32_t>& indexPrimitives, const bool onlyMoveOwnership = false) { PRIM_SETTER(indexPrimitives, SCU32(BufferDefinition::INDICES)) }

			// Helpers for adding useful attributes to Primitive
			void ComputeNormals(NormalComputeMethod computeMode);
			void FlipCulling();
			void FlipNormals();
			// These are to replace memory-stored positions
			//	for performance enhancement, better using pivot transforms (will be implemented)
			void ReoriginToCenter();
			void ReoriginToBottom();

			void Serialize(vz::Archive& archive, const uint64_t version);

			friend struct GeometryComponent;
			friend struct GGeometryComponent;
		};
	protected:
		std::vector<Primitive> parts_;	
		float tessellationFactor_ = 0.f;
		bool isGPUBVHEnabled_ = false;

		// Non-serialized attributes
		bool isDirty_ = true;	// BVH, AABB, ...
		bool hasRenderData_ = false;
		bool hasBVH_ = false;
		geometrics::AABB aabb_; // not serialized (automatically updated)
		std::shared_ptr<std::atomic<bool>> busyUpdateBVH_ = std::make_shared<std::atomic<bool>>(false);

		TimeStamp timeStampPrimitiveUpdate_ = TimerMin;
		TimeStamp timeStampBVHUpdate_ = TimerMin;

		void update();
	public:
		GeometryComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::GEOMETRY, entity, vuid) {}

		bool IsDirtyBVH() const { return TimeDurationCount(timeStampPrimitiveUpdate_, timeStampBVHUpdate_) >= 0; }
		bool HasBVH() const { return hasBVH_; }
		bool IsDirty() { return isDirty_; }
		const geometrics::AABB& GetAABB() { return aabb_; }
		void MovePrimitivesFrom(std::vector<Primitive>&& primitives);
		void CopyPrimitivesFrom(const std::vector<Primitive>& primitives);
		void MovePrimitiveFrom(Primitive&& primitive, const size_t slot);
		void CopyPrimitiveFrom(const Primitive& primitive, const size_t slot);
		void AddMovePrimitiveFrom(Primitive&& primitive);
		void AddCopyPrimitiveFrom(const Primitive& primitive);
		const Primitive* GetPrimitive(const size_t slot) const;
		Primitive* GetMutablePrimitive(const size_t slot);
		const std::vector<Primitive>& GetPrimitives() const { return parts_; }
		size_t GetNumParts() const { return parts_.size(); }
		void SetTessellationFactor(const float tessllationFactor) { tessellationFactor_ = tessllationFactor; }
		float GetTessellationFactor() const { return tessellationFactor_; }

		void UpdateBVH(const bool enabled);

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		// GPU interfaces //
		bool HasRenderData() const { return hasRenderData_; }
		bool IsGPUBVHEnabled() const { return isGPUBVHEnabled_; }
		void SetGPUBVHEnabled(const bool enabled) { isGPUBVHEnabled_ = enabled; }

		virtual void DeleteRenderData() = 0;
		virtual void UpdateRenderData() = 0;
		virtual size_t GetMemoryUsageCPU() const = 0;
		virtual size_t GetMemoryUsageGPU() const = 0;

		inline static const ComponentType IntrinsicType = ComponentType::GEOMETRY;
	};

	struct Histogram
	{
		std::vector<uint64_t> histogram;	// num bins
		float minValue = 0;
		float maxValue = 0;
		float range = 0;
		float range_rcp = 0;
		float numBins = 0;
		float numBins_1 = 0;

		void CreateHistogram(const float minValue, const float maxValue, const size_t numBins)
		{
			assert(numBins > 0 && maxValue > minValue);
			histogram.assign(numBins, 0);
			this->minValue = minValue;
			this->maxValue = maxValue;
			this->numBins = (float)numBins;
			this->numBins_1 = (float)(numBins - 1);
			range = maxValue - minValue;
			range_rcp = 1.f / range;
		}
		inline void CountValue(const float v)
		{
			float normal_v = (v - minValue) * range_rcp;
			if (normal_v < 0 || normal_v > 1) return;
			size_t index = (size_t)(normal_v * numBins_1);
			histogram[index]++;
		}
	};
	struct GTextureInterface;
	constexpr size_t TEXTURE_MAX_RESOLUTION = 4096;
	struct CORE_EXPORT TextureComponent : ComponentBase
	{
		enum class TextureType : uint8_t
		{
			Undefined,
			// this is a buffer (not texture) so no sampler is not used, the case of large-size elements (more than TEXTURE_MAX_RESOLUTION)
			// can be used for high-resolution lookup table (for OTF)
			Buffer,		

			// all textures are allowed for sampler
			Texture1D,	// can be used for low-resolution lookup table (for OTF)
			Texture2D,
			Texture3D,
			Texture2D_Array, // TODO
		};
		// the same as graphics::Format
		enum class TextureFormat : uint8_t
		{
			UNKNOWN,

			R32G32B32A32_FLOAT,
			R32G32B32A32_UINT,
			R32G32B32A32_SINT,

			R32G32B32_FLOAT,
			R32G32B32_UINT,
			R32G32B32_SINT,

			R16G16B16A16_FLOAT,
			R16G16B16A16_UNORM,
			R16G16B16A16_UINT,
			R16G16B16A16_SNORM,
			R16G16B16A16_SINT,

			R32G32_FLOAT,
			R32G32_UINT,
			R32G32_SINT,
			D32_FLOAT_S8X24_UINT,	// depth (32-bit) + stencil (8-bit) | SRV: R32_FLOAT (default or depth aspect), R8_UINT (stencil aspect)

			R10G10B10A2_UNORM,
			R10G10B10A2_UINT,
			R11G11B10_FLOAT,
			R8G8B8A8_UNORM,
			R8G8B8A8_UNORM_SRGB,
			R8G8B8A8_UINT,
			R8G8B8A8_SNORM,
			R8G8B8A8_SINT,
			B8G8R8A8_UNORM,
			B8G8R8A8_UNORM_SRGB,
			R16G16_FLOAT,
			R16G16_UNORM,
			R16G16_UINT,
			R16G16_SNORM,
			R16G16_SINT,
			D32_FLOAT,				// depth (32-bit) | SRV: R32_FLOAT
			R32_FLOAT,
			R32_UINT,
			R32_SINT,
			D24_UNORM_S8_UINT,		// depth (24-bit) + stencil (8-bit) | SRV: R24_INTERNAL (default or depth aspect), R8_UINT (stencil aspect)
			R9G9B9E5_SHAREDEXP,

			R8G8_UNORM,
			R8G8_UINT,
			R8G8_SNORM,
			R8G8_SINT,
			R16_FLOAT,
			D16_UNORM,				// depth (16-bit) | SRV: R16_UNORM
			R16_UNORM,
			R16_UINT,
			R16_SNORM,
			R16_SINT,

			R8_UNORM,
			R8_UINT,
			R8_SNORM,
			R8_SINT,

			// Formats that are not usable in render pass must be below because formats in render pass must be encodable as 6 bits:

			BC1_UNORM,			// Three color channels (5 bits:6 bits:5 bits), with 0 or 1 bit(s) of alpha
			BC1_UNORM_SRGB,		// Three color channels (5 bits:6 bits:5 bits), with 0 or 1 bit(s) of alpha
			BC2_UNORM,			// Three color channels (5 bits:6 bits:5 bits), with 4 bits of alpha
			BC2_UNORM_SRGB,		// Three color channels (5 bits:6 bits:5 bits), with 4 bits of alpha
			BC3_UNORM,			// Three color channels (5 bits:6 bits:5 bits) with 8 bits of alpha
			BC3_UNORM_SRGB,		// Three color channels (5 bits:6 bits:5 bits) with 8 bits of alpha
			BC4_UNORM,			// One color channel (8 bits)
			BC4_SNORM,			// One color channel (8 bits)
			BC5_UNORM,			// Two color channels (8 bits:8 bits)
			BC5_SNORM,			// Two color channels (8 bits:8 bits)
			BC6H_UF16,			// Three color channels (16 bits:16 bits:16 bits) in "half" floating point
			BC6H_SF16,			// Three color channels (16 bits:16 bits:16 bits) in "half" floating point
			BC7_UNORM,			// Three color channels (4 to 7 bits per channel) with 0 to 8 bits of alpha
			BC7_UNORM_SRGB,		// Three color channels (4 to 7 bits per channel) with 0 to 8 bits of alpha

			NV12,				// video YUV420; SRV Luminance aspect: R8_UNORM, SRV Chrominance aspect: R8G8_UNORM
		};
	protected:
		TextureType textureType_ = TextureType::Undefined;
		TextureFormat textureFormat_ = TextureFormat::UNKNOWN;
		uint32_t width_ = 0;
		uint32_t height_ = 0;
		uint32_t depth_ = 0;
		uint32_t arraySize_ = 0;
		uint32_t stride_ = 0;

		// Attributes for mapping table
		XMFLOAT2 tableValidBeginEndX_ = {};

		// file name or arbitrary name as an identifier used by resource manager
		std::string resName_ = "";	

		// sampler 

		// Non-serialized attributes
		std::shared_ptr<Resource> resource_;
		bool hasRenderData_ = false;

	public:
		TextureComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::TEXTURE, entity, vuid) {};
		TextureComponent(const ComponentType ctype, const Entity entity, const VUID vuid = 0) : ComponentBase(ctype, entity, vuid) {}
		
		TextureType GetTextureType() const { return textureType_; }
		bool IsValid() const;

		const std::vector<uint8_t>& GetData() const;
		int GetFontStyle() const;

		std::string GetResourceName() const { return resName_; }
		inline uint32_t GetWidth() const { return width_; }
		inline uint32_t GetHeight() const { return height_; }
		inline uint32_t GetDepth() const { return depth_; }
		inline uint32_t GetArraySize() const { return arraySize_; }
		inline uint32_t GetStride() const { return stride_; }

		void CopyFromData(const std::vector<uint8_t>& data);
		void MoveFromData(std::vector<uint8_t>&& data);

		bool LoadImageFile(const std::string& fileName);
		bool LoadMemory(const std::string& name, 
			const std::vector<uint8_t>& data, const TextureFormat textureFormat,
			const uint32_t w, const uint32_t h, const uint32_t d);
		bool UpdateMemory(const std::vector<uint8_t>& data);

		const XMFLOAT2& GetTableValidBeginEndX() const { return tableValidBeginEndX_; }
		const XMFLOAT2 GetTableValidBeginEndRatioX() const;
		void SetTableValidBeginEndX(const XMFLOAT2& val) { tableValidBeginEndX_ = val; }

		// GPU interfaces //
		bool HasRenderData() const { return hasRenderData_; }

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::TEXTURE;
		friend struct GTextureInterface;
	};

	struct CORE_EXPORT VolumeComponent : TextureComponent
	{
		enum class VolumeFormat : uint8_t
		{
			UNDEF = 0,
			UINT8 = 1,
			UINT16 = 2,
			FLOAT = 4,
		};
	protected:
		std::shared_ptr<Resource> internalBlock_;
		VolumeFormat volFormat_ = VolumeFormat::UNDEF;
		XMFLOAT3 voxelSize_ = {};
		XMFLOAT2 storedMinMax_ = XMFLOAT2(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest());
		XMFLOAT2 originalMinMax_ = XMFLOAT2(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest());

		Histogram histogram_;

		XMFLOAT4X4 matVS2OS_ = math::IDENTITY_MATRIX; // VS to real-sized aligned space
		XMFLOAT4X4 matOS2VS_ = math::IDENTITY_MATRIX;
		XMFLOAT4X4 matVS2TS_ = math::IDENTITY_MATRIX; // VS to texture space
		XMFLOAT4X4 matTS2VS_ = math::IDENTITY_MATRIX; 

		// Non-serialized attributes:
		bool isDirty_ = true; // for matAlign_
	public:
		VolumeComponent(const Entity entity, const VUID vuid = 0) : TextureComponent(ComponentType::VOLUMETEXTURE, entity, vuid) {}

		inline bool IsValidVolume() const;
		inline void SetVoxelSize(const XMFLOAT3& voxelSize) { voxelSize_ = voxelSize; isDirty_ = true; }
		inline void SetStoredMinMax(const XMFLOAT2 minMax) { storedMinMax_ = minMax; }
		inline void SetOriginalMinMax(const XMFLOAT2 minMax) { originalMinMax_ = minMax; }

		inline const XMFLOAT3& GetVoxelSize() const { return voxelSize_; }
		inline float GetMinVoxelSize() const { return std::min({ voxelSize_.x, voxelSize_.y, voxelSize_.z }); }
		inline VolumeFormat GetVolumeFormat() const { return volFormat_; }
		inline const XMFLOAT2& GetStoredMinMax() const { return storedMinMax_; }
		inline const XMFLOAT2& GetOriginalMinMax() const { return originalMinMax_; }
		const Histogram& GetHistogram() const { return histogram_; }
		inline const XMFLOAT4X4& GetMatrixVS2OS() const { return matVS2OS_; }
		inline const XMFLOAT4X4& GetMatrixOS2VS() const { return matOS2VS_; }
		inline const XMFLOAT4X4& GetMatrixVS2TS() const { return matVS2TS_; }
		inline const XMFLOAT4X4& GetMatrixTS2VS() const { return matTS2VS_; }

		inline geometrics::AABB ComputeAABB() const;

		bool LoadVolume(const std::string& fileName, const std::vector<uint8_t>& volData, 
			const uint32_t w, const uint32_t h, const uint32_t d, const VolumeFormat volFormat);

		void UpdateAlignmentMatrix(const XMFLOAT3& axisVolX, const XMFLOAT3& axisVolY, const bool isRHS);
		void UpdateHistogram(const float minValue, const float maxValue, const size_t numBins);

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::VOLUMETEXTURE;
	};

	// scene 
	struct CORE_EXPORT RenderableComponent : ComponentBase
	{
		enum RenderableFlags : uint32_t
		{
			EMPTY = 0,
			MESH_RENDERABLE = 1 << 0,
			REQUEST_PLANAR_REFLECTION = 1 << 4,
			LIGHTMAP_RENDER_REQUEST = 1 << 5,
			LIGHTMAP_DISABLE_BLOCK_COMPRESSION = 1 << 6,
			FOREGROUND = 1 << 7,
			VOLUME_RENDERABLE = 1 << 8,
			CLIP_BOX = 1 << 9,
			CLIP_PLANE = 1 << 10,
			JITTER_SAMPLE = 1 << 11,
		};
	private:
		uint32_t flags_ = RenderableFlags::EMPTY;

		uint8_t visibleLayerMask_ = 0x7;
		VUID vuidGeometry_ = INVALID_ENTITY;
		std::vector<VUID> vuidMaterials_;

		// parameters for visibility effect
		XMFLOAT3 visibleCenter_ = XMFLOAT3(0, 0, 0);
		float visibleRadius_ = 0;
		float fadeDistance_ = std::numeric_limits<float>::max();
		XMFLOAT4 rimHighlightColor_ = XMFLOAT4(1, 1, 1, 0);
		float rimHighlightFalloff_ = 8;

		// clipper
		XMFLOAT4X4 clipBox_ = math::IDENTITY_MATRIX; // WS to origin-centered unit cube
		XMFLOAT4 clipPlane_ = XMFLOAT4(0, 0, 1, 1);

		// Non-serialized attributes:
		//	dirty check can be considered by the following components
		//		- transformComponent, geometryComponent, and material components (with their referencing textureComponents)
		bool isDirty_ = true;
		geometrics::AABB aabb_; // world AABB

		void updateRenderableFlags();
	public:
		RenderableComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::RENDERABLE, entity, vuid) {}

		inline void SetDirty() { isDirty_ = true; }
		inline bool IsDirty() const { return isDirty_; }
		inline bool IsMeshRenderable() const { return flags_ & RenderableFlags::MESH_RENDERABLE; }
		inline bool IsVolumeRenderable() const { return flags_ & RenderableFlags::VOLUME_RENDERABLE; }

		inline void SetForeground(const bool enabled) { FLAG_SETTER(flags_, RenderableFlags::FOREGROUND) }
		inline bool IsForeground() const { return flags_ & RenderableFlags::FOREGROUND; }

		inline uint32_t GetFlags() const { return flags_; }

		inline void SetFadeDistance(const float fadeDistance) { fadeDistance_ = fadeDistance; }
		inline void SetVisibleRadius(const float radius) { visibleRadius_ = radius; }
		inline void SetVisibleCenter(const XMFLOAT3 center) { visibleCenter_ = center; }
		inline void SetGeometry(const Entity geometryEntity);
		inline void SetMaterial(const Entity materialEntity, const size_t slot);
		inline void SetMaterials(const std::vector<Entity>& materials);
		inline void SetVisibleMask(const uint8_t layerBits, const uint8_t maskBits) { SETVISIBLEMASK(visibleLayerMask_, layerBits, maskBits); timeStampSetter_ = TimerNow; }

		inline void EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled) {
			clipBoxEnabled ? flags_ |= RenderableFlags::CLIP_BOX : flags_ &= ~RenderableFlags::CLIP_BOX;
			clipPlaneEnabled ? flags_ |= RenderableFlags::CLIP_PLANE : flags_ &= ~RenderableFlags::CLIP_PLANE;
		}
		inline void SetClipPlane(const XMFLOAT4& clipPlane) { clipPlane_ = clipPlane; }
		inline void SetClipBox(const XMFLOAT4X4& clipBox) { clipBox_ = clipBox; }
		inline bool IsBoxClipperEnabled() const { return flags_ & RenderableFlags::CLIP_BOX; }
		inline bool IsPlaneClipperEnabled() const { return flags_ & RenderableFlags::CLIP_PLANE; };

		inline bool IsVisibleWith(uint8_t visibleLayerMask) const { return visibleLayerMask & visibleLayerMask_; }
		inline uint8_t GetVisibleMask() const { return visibleLayerMask_; }
		inline float GetFadeDistance() const { return fadeDistance_; }
		inline float GetVisibleRadius() const { return visibleRadius_; }
		inline XMFLOAT3 GetVisibleCenter() const { return visibleCenter_; }
		inline XMFLOAT4 GetRimHighLightColor() const { return rimHighlightColor_; }
		inline float GetRimHighLightFalloff() const { return rimHighlightFalloff_; }
		inline XMFLOAT4 GetClipPlane() const { return clipPlane_; }
		inline XMFLOAT4X4 GetClipBox() const { return clipBox_; }

		inline Entity GetGeometry() const;
		inline Entity GetMaterial(const size_t slot) const;
		inline std::vector<Entity> GetMaterials() const;
		inline size_t GetNumParts() const;
		inline size_t GetMaterials(Entity* entities) const;
		inline void Update();
		inline geometrics::AABB GetAABB() const { return aabb_; }

		void ResetRefComponents(const VUID vuidRef) override;
		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::TEXTURE;
	};

	struct CORE_EXPORT LightComponent : ComponentBase
	{
	public:
		enum class LightType : uint32_t {
			DIRECTIONAL = 0,
			POINT,
			SPOT,
			RECT_AREA,
			COUNT
		};
	private:
		enum Flags : uint32_t
		{
			EMPTY = 0,
			CAST_SHADOW = 1 << 0,
			VOLUMETRICS = 1 << 1,
			VISUALIZER = 1 << 2,
		};

		uint32_t lightFlag_ = Flags::EMPTY;
		LightType type_ = LightType::DIRECTIONAL;

		XMFLOAT3 color_ = XMFLOAT3(1, 1, 1);
		float range_ = 10.0f;
		// Brightness of light in. The units that this is defined in depend on the type of light. 
		// Point and spot lights use luminous intensity in candela (lm/sr) while directional lights use illuminance in lux (lm/m2). 
		//  refer to: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual
		float intensity_ = 1.0f; 
		
		// spotlight only
		float radius_ = 0.25; 
		float length_ = 0.f;
		float outerConeAngle_ = XM_PIDIV4;
		float innerConeAngle_ = 0; // default value is 0, means only outer cone angle is used

		// Non-serialized attributes:
		bool isDirty_ = true;

		geometrics::AABB aabb_;

		// note there will be added many attributes to describe the light properties with various lighting techniques
		// refer to filament engine's lightManager and wicked engine's lightComponent
	public:
		LightComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::LIGHT, entity, vuid) {}

		// Non-serialized attributes:
		// if there is no transformComponent, then use these attributes directly
		// unless, these attributes will be automatically updated during the scene update
		mutable int occlusionquery = -1;

		// Non-serialized attributes: (these variables are supposed to be updated via transformers)
		// read-only
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		XMFLOAT3 direction = XMFLOAT3(0, 1, 0);
		XMFLOAT4 rotation = XMFLOAT4(0, 0, 0, 1);
		XMFLOAT3 scale = XMFLOAT3(1, 1, 1);

		inline bool IsDirty() const { return isDirty_; }

		inline void SetDirty() { isDirty_ = true; }
		inline void SetLightColor(XMFLOAT3 color) { color_ = color; timeStampSetter_ = TimerNow; }
		inline void SetRange(const float range) { range_ = range; isDirty_ = true; timeStampSetter_ = TimerNow; }
		inline void SetRadius(const float radius) { radius_ = radius; timeStampSetter_ = TimerNow; }
		inline void SetLength(const float length) { length_ = length; timeStampSetter_ = TimerNow; }
		inline void SetLightType(LightType type) { type_ = type; isDirty_ = true; timeStampSetter_ = TimerNow; };
		inline void SetLightIntensity(const float intensity) { intensity_ = intensity; }
		inline void SetOuterConeAngle(const float angle) { outerConeAngle_ = angle; isDirty_ = true; timeStampSetter_ = TimerNow;}
		inline void SetInnerConeAngle(const float angle) { innerConeAngle_ = angle; isDirty_ = true; timeStampSetter_ = TimerNow; }

		inline XMFLOAT3 GetLightColor() const { return color_; }
		inline float GetLightIntensity() const { return intensity_; }
		inline float GetRange() const
		{
			float retval = range_;
			retval = std::max(0.001f, retval);
			retval = std::min(retval, 65504.0f); // clamp to 16-bit float max value
			return retval;
		}
		inline float GetRadius() const { return radius_; }
		inline float GetLength() const { return length_; }
		inline const geometrics::AABB& GetAABB() const { return aabb_; }
		inline LightType GetLightType() const { return type_; }
		inline float GetOuterConeAngle() const { return outerConeAngle_; }
		inline float GetInnerConeAngle() const { return innerConeAngle_; }

		inline bool IsInactive() const { return intensity_ == 0 || range_ == 0; }

		inline void Update();	// if there is a transform entity, make sure the transform is updated!

		inline void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::LIGHT;
	};

	struct CORE_EXPORT CameraComponent : ComponentBase
	{
	private:
		enum CamFlags : uint8_t
		{
			EMPTY = 0,
			ORTHOGONAL = 1 << 0,    // if not, PERSPECTIVE
			CUSTOM_PROJECTION = 1 << 2,
			CLIP_PLANE = 1 << 3,
			CLIP_BOX = 1 << 4
		};

		float zNearP_ = 0.1f;
		float zFarP_ = 5000.0f;
		float fovY_ = XM_PI / 3.0f;
		float focalLength_ = 1;
		float apertureSize_ = 0;
		float orthoVerticalSize_ = 1.f;
		XMFLOAT2 apertureShape_ = XMFLOAT2(1, 1);

		// camera lens/sensor settings: used for the postprocess chain
		//  note: resolution and colorspace settings are involved in renderer (RenderPath3D)
		float exposure_ = 1.f;
		float brightness_ = 0.f;
		float contrast_ = 1.f;
		float saturation_ = 1.f;
		float hdrCalibration_ = 1.f;

		bool eyeAdaptionEnabled_ = false;
		bool bloomEnabled_ = false;

		// These parameters are used differently depending on the projection mode.
		// 1. orthogonal : image's width and height (canvas size)
		// 2. perspective : computing aspect (W / H) ratio, i.e., (width_, height_) := (aspectRatio, 1.f)
		// NOTE: these are NOT buffer or texture resolution!
		float width_ = 0.0f;
		float height_ = 0.0f;

		uint8_t visibleLayerMask_ = ~0;
		uint8_t flags_ = CamFlags::EMPTY;

		// clipper
		XMFLOAT4X4 clipBox_ = math::IDENTITY_MATRIX; // WS to origin-centered unit cube
		XMFLOAT4 clipPlane_ = XMFLOAT4(0, 0, 1, 1);

		// Non-serialized attributes:
		bool isDirty_ = true;
		XMFLOAT3 eye_ = XMFLOAT3(0, 0, 0);
		XMFLOAT3 at_ = XMFLOAT3(0, 0, 1);
		XMFLOAT3 forward_ = XMFLOAT3(0, 0, 1); // viewing direction
		XMFLOAT3 up_ = XMFLOAT3(0, 1, 0);
		XMFLOAT3X3 rotationMatrix_ = math::IDENTITY_MATRIX33;
		XMFLOAT4X4 view_, projection_, viewProjection_;
		XMFLOAT4X4 invView_, invProjection_, invViewProjection_;
		vz::geometrics::Frustum frustum_ = {};

		float computeOrthoVerticalSizeFromPerspective(const float dist);

	public:
		CameraComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::CAMERA, entity, vuid) {
			view_ = projection_ = viewProjection_ = invView_ = invProjection_ = invViewProjection_ = math::IDENTITY_MATRIX;
		}

		// Non-serialized attributes:
		XMFLOAT2 jitter = XMFLOAT2(0, 0);

		inline void SetDirty() { isDirty_ = true; }
		inline bool IsDirty() const { return isDirty_; }
		inline uint8_t GetVisibleLayerMask() const { return visibleLayerMask_; }
		inline void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits) { SETVISIBLEMASK(visibleLayerMask_, layerBits, maskBits); }

		// consider TransformComponent and HierarchyComponent that belong to this CameraComponent entity
		inline bool SetWorldLookAtFromHierarchyTransforms();
		inline void SetWorldLookAt(const XMFLOAT3& eye, const XMFLOAT3& at, const XMFLOAT3& up);
		inline void SetWorldLookTo(const XMFLOAT3& eye, const XMFLOAT3& view, const XMFLOAT3& up) {
			eye_ = eye; XMStoreFloat3(&at_, XMLoadFloat3(&eye) + XMLoadFloat3(&view)); up_ = up; forward_ = view;
			SetWorldLookAt(eye_, at_, up_);
			isDirty_ = true;
			timeStampSetter_ = TimerNow;
		}
		inline void SetPerspective(const float width, const float height, const float nearP, const float farP, const float fovY = XM_PI / 3.0f) {
			width_ = width; height_ = height; zNearP_ = nearP; zFarP_ = farP; fovY_ = fovY; 
			flags_ &= ~ORTHOGONAL;
			isDirty_ = true; timeStampSetter_ = TimerNow;
		}
		inline void SetOrtho(const float width, const float height, const float nearP, const float farP, const float orthoVerticalSize) {
			width_ = width; height_ = height; zNearP_ = nearP; zFarP_ = farP; 
			if (orthoVerticalSize < 0)
				orthoVerticalSize_ = computeOrthoVerticalSizeFromPerspective(math::Length(eye_));
			else if (orthoVerticalSize > 0)
				orthoVerticalSize_ = orthoVerticalSize;
			flags_ |= ORTHOGONAL;
			isDirty_ = true; timeStampSetter_ = TimerNow;
		}

		inline void EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled) {
			clipBoxEnabled ? flags_ |= CamFlags::CLIP_BOX : flags_ &= ~CamFlags::CLIP_BOX;
			clipPlaneEnabled ? flags_ |= CamFlags::CLIP_PLANE : flags_ &= ~CamFlags::CLIP_PLANE;
		}
		inline void SetClipPlane(const XMFLOAT4& clipPlane) { clipPlane_ = clipPlane; }
		inline void SetClipBox(const XMFLOAT4X4& clipBox) { clipBox_ = clipBox; }
		bool IsBoxClipperEnabled() const { return flags_ & CamFlags::CLIP_BOX; }
		bool IsPlaneClipperEnabled() const { return flags_ & CamFlags::CLIP_PLANE; };

		// update view matrix using camera extrinsics such as eye_, at_, and up_ set by the above setters
		// update proj matrix using camera intrinsics
		// update view-proj and their inverse matrices using the updated view and proj matrices
		inline void UpdateMatrix();

		inline const XMFLOAT3& GetWorldEye() const { return eye_; }
		inline const XMFLOAT3& GetWorldAt() const { return at_; }
		inline const XMFLOAT3& GetWorldForward() const { return forward_; }
		inline const XMFLOAT3& GetWorldUp() const { return up_; }
		inline const XMFLOAT3X3& GetWorldRotation() const { return rotationMatrix_; }
		inline const XMFLOAT4X4& GetView() const { return view_; }
		inline const XMFLOAT4X4& GetProjection() const { return projection_; }
		inline const XMFLOAT4X4& GetViewProjection() const { return viewProjection_; }
		inline const XMFLOAT4X4& GetInvView() const { return invView_; }
		inline const XMFLOAT4X4& GetInvProjection() const { return invProjection_; }
		inline const XMFLOAT4X4& GetInvViewProjection() const { return invViewProjection_; }
		inline const geometrics::Frustum& GetFrustum() const { return frustum_; }

		inline bool IsOrtho() const { return flags_ & ORTHOGONAL; }
		inline float GetFovVertical() const { return fovY_; }
		inline float GetFocalLength() const { return focalLength_; }
		inline float GetApertureSize() const { return apertureSize_; }
		inline XMFLOAT2 GetApertureShape() const { return apertureShape_; }

		inline void GetWidthHeight(float* w, float* h) const { if (w) *w = width_; if (h) *h = height_; }
		inline void GetNearFar(float* n, float* f) const { if (n) *n = zNearP_; if (f) *f = zFarP_; }
		inline float GetOrthoVerticalSize() const { return orthoVerticalSize_; }

		inline XMFLOAT4 GetClipPlane() const { return clipPlane_; }
		inline XMFLOAT4X4 GetClipBox() const { return clipBox_; }

		// camera lens and sensor setting
		inline void SensorExposure(const float exposure) { exposure_ = exposure; timeStampSetter_ = TimerNow; }
		inline void SensorBrightness(const float brightness) { brightness_ = brightness; timeStampSetter_ = TimerNow; }
		inline void SensorContrast(const float contrast) { contrast_ = contrast; timeStampSetter_ = TimerNow; }
		inline void SensorSaturation(const float saturation) { saturation_ = saturation; timeStampSetter_ = TimerNow; }
		inline void SensorEyeAdaptationEnabled(const bool eyeAdaptionEnabled) { eyeAdaptionEnabled_ = eyeAdaptionEnabled; timeStampSetter_ = TimerNow; }
		inline void SensorBloomEnabled(const bool bloomEnabled) { bloomEnabled_ = bloomEnabled; timeStampSetter_ = TimerNow; }
		inline void SensorHdrCalibration(const float hdrCalibration) { hdrCalibration_ = hdrCalibration; timeStampSetter_ = TimerNow; }

		inline float GetSensorExposure() const { return exposure_; }
		inline float GetSensorBrightness() const { return brightness_; }
		inline float GetSensorContrast() const { return contrast_; }
		inline float GetSensorSaturation() const { return saturation_; }
		inline bool IsSensorEyeAdaptationEnabled() const { return eyeAdaptionEnabled_; }
		inline bool IsSensorBloomEnabled() const { return bloomEnabled_; }
		inline float GetSensorHdrCalibration() const { return hdrCalibration_; }

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::CAMERA;
	};
}

// component factory
namespace vz::compfactory
{
	// EngineFactory class (API)
	// 1. singleton (when initializing engine APIs)
	// 2. getter
	// 3. destroy all (or leaking message)

	// here, inlining is actually applied only when building the same object file
	// calling in other built object files ignores the inlining

	// VUID Manager
	CORE_EXPORT inline ComponentBase* GetComponentByVUID(const VUID vuid);
	CORE_EXPORT inline Entity GetEntityByVUID(const VUID vuid);

	// Component Manager
	CORE_EXPORT inline size_t SetSceneComponentsDirty(const Entity entity);

	CORE_EXPORT inline NameComponent* CreateNameComponent(const Entity entity, const std::string& name);
	CORE_EXPORT inline TransformComponent* CreateTransformComponent(const Entity entity);
	CORE_EXPORT inline HierarchyComponent* CreateHierarchyComponent(const Entity entity, const Entity parent = INVALID_ENTITY);
	CORE_EXPORT inline MaterialComponent* CreateMaterialComponent(const Entity entity);
	CORE_EXPORT inline GeometryComponent* CreateGeometryComponent(const Entity entity);
	CORE_EXPORT inline TextureComponent* CreateTextureComponent(const Entity entity);
	CORE_EXPORT inline VolumeComponent* CreateVolumeComponent(const Entity entity);
	CORE_EXPORT inline LightComponent* CreateLightComponent(const Entity entity);
	CORE_EXPORT inline CameraComponent* CreateCameraComponent(const Entity entity);
	CORE_EXPORT inline RenderableComponent* CreateRenderableComponent(const Entity entity);

	CORE_EXPORT inline NameComponent* GetNameComponent(const Entity entity);
	CORE_EXPORT inline TransformComponent* GetTransformComponent(const Entity entity);
	CORE_EXPORT inline HierarchyComponent* GetHierarchyComponent(const Entity entity);
	CORE_EXPORT inline MaterialComponent* GetMaterialComponent(const Entity entity);
	CORE_EXPORT inline GeometryComponent* GetGeometryComponent(const Entity entity);
	CORE_EXPORT inline TextureComponent* GetTextureComponent(const Entity entity);
	CORE_EXPORT inline VolumeComponent* GetVolumeComponent(const Entity entity);
	CORE_EXPORT inline RenderableComponent* GetRenderableComponent(const Entity entity);
	CORE_EXPORT inline LightComponent* GetLightComponent(const Entity entity);
	CORE_EXPORT inline CameraComponent* GetCameraComponent(const Entity entity);

	CORE_EXPORT inline NameComponent* GetNameComponentByVUID(const VUID vuid);
	CORE_EXPORT inline TransformComponent* GetTransformComponentByVUID(const VUID vuid);
	CORE_EXPORT inline HierarchyComponent* GetHierarchyComponentByVUID(const VUID vuid);
	CORE_EXPORT inline MaterialComponent* GetMaterialComponentByVUID(const VUID vuid);
	CORE_EXPORT inline GeometryComponent* GetGeometryComponentByVUID(const VUID vuid);
	CORE_EXPORT inline TextureComponent* GetTextureComponentByVUID(const VUID vuid);
	CORE_EXPORT inline VolumeComponent* GetVolumeComponentByVUID(const VUID vuid);
	CORE_EXPORT inline RenderableComponent* GetRenderableComponentByVUID(const VUID vuid);
	CORE_EXPORT inline LightComponent* GetLightComponentByVUID(const VUID vuid);
	CORE_EXPORT inline CameraComponent* GetCameraComponentByVUID(const VUID vuid);

	CORE_EXPORT inline size_t GetTransformComponents(const std::vector<Entity>& entities, std::vector<TransformComponent*>& comps);
	CORE_EXPORT inline size_t GetHierarchyComponents(const std::vector<Entity>& entities, std::vector<HierarchyComponent*>& comps);
	CORE_EXPORT inline size_t GetMaterialComponents(const std::vector<Entity>& entities, std::vector<MaterialComponent*>& comps);
	CORE_EXPORT inline size_t GetLightComponents(const std::vector<Entity>& entities, std::vector<LightComponent*>& comps);

	CORE_EXPORT inline bool ContainNameComponent(const Entity entity);
	CORE_EXPORT inline bool ContainTransformComponent(const Entity entity);
	CORE_EXPORT inline bool ContainHierarchyComponent(const Entity entity);
	CORE_EXPORT inline bool ContainMaterialComponent(const Entity entity);
	CORE_EXPORT inline bool ContainGeometryComponent(const Entity entity);
	CORE_EXPORT inline bool ContainRenderableComponent(const Entity entity);
	CORE_EXPORT inline bool ContainLightComponent(const Entity entity);
	CORE_EXPORT inline bool ContainCameraComponent(const Entity entity);
	CORE_EXPORT inline bool ContainTextureComponent(const Entity entity);
	CORE_EXPORT inline bool ContainVolumeComponent(const Entity entity);

	CORE_EXPORT inline size_t GetComponents(const Entity entity, std::vector<ComponentBase*>& components);
	CORE_EXPORT inline size_t GetEntitiesByName(const std::string& name, std::vector<Entity>& entities); // when there is a name component
	CORE_EXPORT Entity GetFirstEntityByName(const std::string& name);

	CORE_EXPORT size_t Destroy(const Entity entity);
}
