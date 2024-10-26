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

using GpuHandler = uint32_t;
using Entity = uint32_t;
using VUID = uint64_t;
inline constexpr Entity INVALID_ENTITY = 0;
inline constexpr VUID INVALID_VUID = 0;
using TimeStamp = std::chrono::high_resolution_clock::time_point;
#define TimeDurationCount(A, B) std::chrono::duration_cast<std::chrono::duration<double>>(A - B).count()
#define TimerNow std::chrono::high_resolution_clock::now()
#define TimerMin std::chrono::high_resolution_clock::time_point::min();
#define SETVISIBLEMASK(MASK, LAYERBITS, MASKBITS) MASK = (MASK & ~LAYERBITS) | (MASKBITS & LAYERBITS);

namespace vz
{
	inline static const std::string COMPONENT_INTERFACE_VERSION = "VZ::20241023";
	inline static std::string stringEntity(Entity entity) { return "(" + std::to_string(entity) + ")"; }
	CORE_EXPORT std::string GetComponentVersion();

	class Archive;
	struct GScene;

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
				if (_entities[i] = targetEntity)
				{
					return i;
				}
			}
			return ~0u;
		}

	protected:
		std::string name_;

		// Instead of Entity, VUID is stored by serialization
		//	the index is same to the streaming index
		std::vector<Entity> renderables_;
		std::vector<Entity> lights_;

		// TODO
		// camera for reflection 

		// Non-serialized attributes:
		std::unordered_map<Entity, size_t> lookupRenderables_; // each entity has also TransformComponent and HierarchyComponent
		std::unordered_map<Entity, size_t> lookupLights_;

		geometrics::AABB aabb_;

		Entity entity_ = INVALID_ENTITY;
		TimeStamp recentUpdateTime_ = TimerMin;	// world update time
		TimeStamp timeStampSetter_ = TimerMin;
		
		bool isDirty_ = true;

		// instant parameters during render-process
		float dt_ = 0.f;

		GScene* handlerScene_ = nullptr;

	public:
		Scene(const Entity entity, const std::string& name);
		~Scene();

		void SetDirty() { isDirty_ = true; }
		bool IsDirty() const { return isDirty_; }

		inline const std::string GetSceneName() const { return name_; }
		inline const Entity GetSceneEntity() const { return entity_; }
		inline const GScene* GetGSceneHandle() const { return handlerScene_; }

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
		inline std::vector<Entity> ScanGeometryEntities() const noexcept;
		inline std::vector<Entity> ScanMaterialEntities() const noexcept;

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

		/**
		 * Read/write scene components (renderbles and lights), make sure their VUID-based components are serialized first
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

	enum class DataType : uint8_t 
	{
		UNDEFINED = 0,
		BOOL,
		CHAR,
		CHAR2,
		CHAR3,
		CHAR4,
		BYTE,
		BYTE2,
		BYTE3,
		BYTE4,
		SHORT,
		SHORT2,
		SHORT3,
		SHORT4,
		USHORT,
		USHORT2,
		USHORT3,
		USHORT4,
		FLOAT,
		FLOAT2,
		FLOAT3,
		FLOAT4,
		INT,
		INT2,
		INT3,
		INT4,
		UINT,
		UINT2,
		UINT3,
		UINT4,
		MAT3,   //!< a 3x3 float matrix
		MAT4,   //!< a 4x4 float matrix
		STRUCT
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
		//bool IsLocked() const { return isLocked_; }
		//void TryLock() { isLocked_ = { true }; }

		virtual void Serialize(vz::Archive& archive, const uint64_t version) = 0;
	};

	struct CORE_EXPORT NameComponent : ComponentBase
	{
		NameComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::NAME, entity, vuid) {}

		std::string name;

		inline void operator=(const std::string& str) { name = str; }
		inline void operator=(std::string&& str) { name = std::move(str); }
		inline bool operator==(const std::string& str) const { return name.compare(str) == 0; }

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
		
		// Non-serialized attributes:
		bool isDirty_ = true; // local check
		// The local matrix can be computed from local scale, rotation, translation 
		//	- by calling UpdateMatrix()
		//	- or by isDirty_ := false and letting the TransformUpdateSystem handle the updating
		XMFLOAT4X4 local_ = math::IDENTITY_MATRIX;

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
		std::unordered_set<VUID> children_;

		// Non-serialized attributes
		std::vector<VUID> childrenCache_;
		inline void updateChildren();

	public:
		HierarchyComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::HIERARCHY, entity, vuid) {}

		inline void SetParent(const VUID vuidParent);
		inline VUID GetParent() const;
		inline Entity GetParentEntity() const;

		inline void AddChild(const VUID vuidChild);
		inline void RemoveChild(const VUID vuidChild);
		inline std::vector<VUID> GetChildren() { if (children_.size() != childrenCache_.size()) updateChildren(); return childrenCache_; }

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
		};
		enum class ShaderType : uint32_t
		{
			PHONG = 0,
			PBR,
			UNLIT,

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

			VOLUME_DENSITYMAP, // this is used for volume rendering

			TEXTURESLOT_COUNT
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

	protected:
		uint32_t flags_ = (uint32_t)RenderFlags::FORWARD;
		ShaderType shaderType_ = ShaderType::PHONG;
		BlendMode blendMode_ = BlendMode::BLENDMODE_OPAQUE;
		StencilRef engineStencilRef_ = StencilRef::STENCILREF_DEFAULT;

		float alphaRef_ = 1.f;
		XMFLOAT4 baseColor_ = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 specularColor_ = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 emissiveColor_ = XMFLOAT4(1, 1, 1, 0);

		XMFLOAT4 phongFactors_ = XMFLOAT4(0.2f, 1, 1, 1);	// only used for ShaderType::PHONG

		VUID textureComponents_[SCU32(TextureSlot::TEXTURESLOT_COUNT)] = {};

		XMFLOAT4 texMulAdd_ = XMFLOAT4(1, 1, 0, 0);

		// Non-serialized Attributes:
		bool isDirty_ = true;
	public:
		MaterialComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::MATERIAL, entity, vuid) {}

		inline const float GetOpacity() const { return baseColor_.w; }
		inline const XMFLOAT4& GetBaseColor() const { return baseColor_; }	// w is opacity
		inline const XMFLOAT4& GetSpecularColor() const { return specularColor_; }
		inline const XMFLOAT4& GetEmissiveColor() const { return emissiveColor_; }	// w is emissive strength

		void SetAlphaRef(const float alphaRef) { alphaRef_ = alphaRef; }
		inline void SetBaseColor(const XMFLOAT4& baseColor) { baseColor_ = baseColor; isDirty_ = true; }
		inline void SetSpecularColor(const XMFLOAT4& specularColor) { specularColor_ = specularColor; isDirty_ = true; }
		inline void SetEmissiveColor(const XMFLOAT4& emissiveColor) { emissiveColor_ = emissiveColor; isDirty_ = true; }
		inline void EnableWetmap(const bool enabled) { FLAG_SETTER(flags_, RenderFlags::WETMAP) isDirty_ = true; }
		inline void SetCastShadow(bool enabled) { FLAG_SETTER(flags_, RenderFlags::CAST_SHADOW) isDirty_ = true; }
		inline void SetReceiveShadow(bool enabled) { FLAG_SETTER(flags_, RenderFlags::RECEIVE_SHADOW) isDirty_ = true; }

		inline void SetTexture(const Entity textureEntity, const TextureSlot textureSlot);

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

		inline uint32_t GetRenderFlags() const { return flags_; }

		inline StencilRef GetStencilRef() const { return engineStencilRef_; }

		inline float GetAlphaRef() const { return alphaRef_; }
		inline BlendMode GetBlendMode() const { return blendMode_; }
		inline VUID GetTextureVUID(const size_t slot) const { 
			if (slot >= SCU32(TextureSlot::TEXTURESLOT_COUNT)) return INVALID_VUID; return textureComponents_[slot];
		}
		inline const XMFLOAT4 GetTexMulAdd() const { return texMulAdd_; }
		inline ShaderType GetShaderType() const { return shaderType_; }

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
			TRIANGLE_STRIP = 4     //!< triangle strip
		};
		enum class COMPUTE_NORMALS : uint8_t {
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

		struct Primitive {
		private:
			bool isValid_[SCU32(BufferDefinition::COUNT)] = { }; // false

			std::vector<XMFLOAT3> vertexPositions_;
			std::vector<XMFLOAT3> vertexNormals_;
			std::vector<XMFLOAT4> vertexTangents_;
			std::vector<XMFLOAT2> vertexUVset0_;
			std::vector<XMFLOAT2> vertexUVset1_;
			std::vector<uint32_t> vertexColors_;
			std::vector<uint32_t> indexPrimitives_;

			PrimitiveType ptype_ = PrimitiveType::TRIANGLES;

			// Non-serialized Attributes:
			geometrics::AABB aabb_;
			XMFLOAT2 uvRangeMin_ = XMFLOAT2(0, 0);
			XMFLOAT2 uvRangeMax_ = XMFLOAT2(1, 1);
			std::shared_ptr<void> bufferHandle_;	// 'void' refers to GGeometryComponent::GBuffer
			Entity recentBelongingGeometry_ = INVALID_ENTITY;

			void updateGpuEssentials(); // supposed to be called in GeometryComponent

		public:
			mutable bool autoUpdateRenderData = true;

			inline void MoveFrom(Primitive&& primitive)
			{
				*this = std::move(primitive);
				//vertexPositions_ = std::move(primitive.vertexPositions_);
				//vertexNormals_ = std::move(primitive.vertexNormals_);
				//vertexTangents_ = std::move(primitive.vertexTangents_);
				//vertexUVset0_ = std::move(primitive.vertexUVset0_);
				//vertexUVset1_ = std::move(primitive.vertexUVset1_);
				//vertexColors_ = std::move(primitive.vertexColors_);
				//indexPrimitives_ = std::move(primitive.indexPrimitives_);
				//bufferHandle_ = std::move(primitive.bufferHandle_);
				//
				//aabb_ = primitive.aabb_;
				//ptype_ = primitive.ptype_;
				//
				//if (vertexPositions_.size() > 0) isValid_[SCU32(BufferDefinition::POSITION)] = true;
				//if (vertexNormals_.size() > 0 && vertexNormals_.size() == vertexPositions_.size()) isValid_[SCU32(BufferDefinition::NORMAL)] = true;
				//if (vertexTangents_.size() > 0 && vertexTangents_.size() == vertexPositions_.size()) isValid_[SCU32(BufferDefinition::TANGENT)] = true;
				//if (vertexUVset0_.size() > 0 && vertexUVset0_.size() == vertexPositions_.size()) isValid_[SCU32(BufferDefinition::UVSET0)] = true;
				//if (vertexUVset1_.size() > 0 && vertexUVset1_.size() == vertexPositions_.size()) isValid_[SCU32(BufferDefinition::UVSET1)] = true;
				//if (vertexColors_.size() > 0 && vertexColors_.size() == vertexPositions_.size()) isValid_[SCU32(BufferDefinition::COLOR)] = true;
				//if (indexPrimitives_.size() > 0) isValid_[SCU32(BufferDefinition::INDICES)] = true;
			}
			inline void MoveTo(Primitive& primitive)
			{
				primitive = std::move(*this);
				*this = Primitive();
				//primitive.vertexPositions_ = std::move(vertexPositions_);
				//primitive.vertexNormals_ = std::move(vertexNormals_);
				//primitive.vertexTangents_ = std::move(vertexTangents_);
				//primitive.vertexUVset0_ = std::move(vertexUVset0_);
				//primitive.vertexUVset1_ = std::move(vertexUVset1_);
				//primitive.vertexColors_ = std::move(vertexColors_);
				//primitive.indexPrimitives_ = std::move(indexPrimitives_);
				//primitive.aabb_ = aabb_;
				//primitive.ptype_ = ptype_;
				//for (size_t i = 0, n = SCU32(BufferDefinition::COUNT); i < n; ++i) isValid_[i] = false;
			}
			inline const geometrics::AABB& GetAABB() const { return aabb_; }
			inline geometrics::Sphere GetBoundingSphere() const
			{
				geometrics::Sphere sphere;
				sphere.center = aabb_.getCenter();
				sphere.radius = aabb_.getRadius();
				return sphere;
			}
			inline PrimitiveType GetPrimitiveType() const { return ptype_; }
			inline bool IsValid() const { return isValid_[SCU32(BufferDefinition::POSITION)] && aabb_.IsValid(); }
			inline void SetAABB(const geometrics::AABB& aabb) { aabb_ = aabb; }
			inline void SetPrimitiveType(const PrimitiveType ptype) { ptype_ = ptype; }

			// ----- Getters -----
			inline const std::vector<XMFLOAT3>& GetVtxPositions() const { assert(isValid_[SCU32(BufferDefinition::POSITION)]); return vertexPositions_; }
			inline const std::vector<uint32_t>& GetIdxPrimives() const { assert(isValid_[SCU32(BufferDefinition::INDICES)]); return indexPrimitives_; }
			inline const std::vector<XMFLOAT3>& GetVtxNormals() const { assert(isValid_[SCU32(BufferDefinition::NORMAL)]); return vertexNormals_; }
			inline const std::vector<XMFLOAT4>& GetVtxTangents() const { assert(isValid_[SCU32(BufferDefinition::TANGENT)]); return vertexTangents_; }
			inline const std::vector<XMFLOAT2>& GetVtxUVSet0() const { assert(isValid_[SCU32(BufferDefinition::UVSET0)]); return vertexUVset0_; }
			inline const std::vector<XMFLOAT2>& GetVtxUVSet1() const { assert(isValid_[SCU32(BufferDefinition::UVSET1)]); return vertexUVset1_; }
			inline const std::vector<uint32_t>& GetVtxColors() const { assert(isValid_[SCU32(BufferDefinition::COLOR)]); return vertexColors_; }

			inline size_t GetNumVertices() const { return vertexPositions_.size(); }
			inline size_t GetNumIndices() const { return indexPrimitives_.size(); }

			inline const XMFLOAT2& GetUVRangeMin() const { return uvRangeMin_; }
			inline const XMFLOAT2& GetUVRangeMax() const { return uvRangeMax_; }

			#define PRIM_SETTER(A, B) A##_ = onlyMoveOwnership ? std::move(A) : A; isValid_[B] = true;
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
			void ComputeNormals(COMPUTE_NORMALS computeMode);
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

		// Non-serialized attributes
		bool isDirty_ = true;	// BVH, AABB, ...
		bool hasRenderData_ = false;
		geometrics::AABB aabb_; // not serialized (automatically updated)
		//bool hasBVH_ = false;

		void update();
	public:
		GeometryComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::GEOMETRY, entity, vuid) {}

		bool IsDirty() { return isDirty_; }
		const geometrics::AABB& GetAABB() { return aabb_; }
		void MovePrimitivesFrom(std::vector<Primitive>&& primitives);
		void CopyPrimitivesFrom(const std::vector<Primitive>& primitives);
		void MovePrimitiveFrom(Primitive&& primitive, const size_t slot);
		void CopyPrimitiveFrom(const Primitive& primitive, const size_t slot);
		const Primitive* GetPrimitive(const size_t slot) const;
		const std::vector<Primitive>& GetPrimitives() const { return parts_; }
		size_t GetNumParts() const { return parts_.size(); }
		void SetTessellationFactor(const float tessllationFactor) { tessellationFactor_ = tessllationFactor; }
		float GetTessellationFactor() const { return tessellationFactor_; }

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		// GPU interfaces //
		bool HasRenderData() const { return hasRenderData_; }
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

		void CreateHistogram(const float minValue, const float maxValue, const size_t numBins)
		{
			histogram.assign(numBins, 0);
			this->minValue = minValue;
			this->maxValue = maxValue;
			this->numBins = (float)numBins;
			range = maxValue - minValue;
			range_rcp = 1.f / range;
		}
		inline void CountValue(const float v)
		{
			float normal_v = (v - minValue) * range_rcp;
			if (normal_v < 0 || normal_v > 1) return;
			size_t index = (size_t)(normal_v * numBins) - 1;
			histogram[index]++;
		}
	};
	struct Resource;
	constexpr size_t TEXTURE_MAX_RESOLUTION = 4096;
	struct CORE_EXPORT TextureComponent : virtual ComponentBase
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
			TextureEnv, // TODO
		};
	protected:
		TextureType textureType_ = TextureType::Undefined;
		DataType dataType_ = DataType::UNDEFINED;
		std::shared_ptr<Resource> internalResource_;
		uint32_t width_ = 1;
		uint32_t height_ = 1;
		uint32_t depth_ = 1;
		uint32_t arraySize_ = 1;

		// sampler 
	public:
		TextureComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::TEXTURE, entity, vuid) {}
		//TextureComponent(const ComponentType type, const Entity entity, const VUID vuid = 0) : ComponentBase(type, entity, vuid) {}
		
		TextureType GetTextureType() const { return textureType_; }
		bool IsValid() const;

		const std::vector<uint8_t>& GetData() const;
		int GetFontStyle() const;
		void CopyFromData(const std::vector<uint8_t>& data);
		void MoveFromData(std::vector<uint8_t>&& data);
		void SetOutdated();	// to avoid automatic renderdata update

		inline uint32_t GetWidth() const { return width_; }
		inline uint32_t GetHeight() const { return height_; }
		inline uint32_t GetDepth() const { return depth_; }
		inline uint32_t GetArraySize() const { return arraySize_; }
		inline void SetTextureDimension(const uint32_t w, const uint32_t h, const uint32_t d, const uint32_t array) {
			width_ = w, height_ = h, depth_ = d, arraySize_ = array;
		}
		inline void SetDataType(const DataType dtype) { dataType_ = dtype; }
		inline DataType GetDataType() const { return dataType_; }
		
		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::TEXTURE;
	};

	struct CORE_EXPORT VolumeComponent : virtual TextureComponent
	{
	protected:
		std::shared_ptr<Resource> internalBlock_;
		XMFLOAT3 voxelSize_ = {};
		DataType originalDataType_ = DataType::UNDEFINED;
		XMFLOAT2 storedMinMax_ = XMFLOAT2(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest());
		XMFLOAT2 originalMinMax_ = XMFLOAT2(std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest());

		Histogram histogram_;

		XMFLOAT4X4 matAlign_ = math::IDENTITY_MATRIX; // VS to real-sized aligned space

		// sampler 
	public:
		VolumeComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::VOLUMETEXTURE, entity, vuid), TextureComponent(entity, vuid) {}

		inline void SetVoxelSize(const XMFLOAT3& voxelSize) { voxelSize_ = voxelSize; }
		inline XMFLOAT3 GetVoxelSize() const { return voxelSize_; }
		inline float GetMinVoxelSize() const { return std::min({voxelSize_.x, voxelSize_.y, voxelSize_.z}); }

		inline void SetOriginalDataType(const DataType originalDataType) { originalDataType_ = originalDataType; }
		inline DataType GetOriginalDataType() const { return originalDataType_; }

		inline void SetStoredMinMax(const XMFLOAT2 minMax) { storedMinMax_ = minMax; }
		inline XMFLOAT2 GetStoredMinMax() const { return storedMinMax_; }
		inline void SetOriginalMinMax(const XMFLOAT2 minMax) { originalMinMax_ = minMax; }
		inline XMFLOAT2 GetOriginalMinMax() const { return originalMinMax_; }

		const Histogram& GetHistogram() const { return histogram_; }
		void UpdateHistogram(const float minValue, const float maxValue, const size_t numBins);

		inline void SetAlign(const XMFLOAT3& axisVolX, const XMFLOAT3& axisVolY, const bool isRHS);
		inline XMFLOAT4X4 GetAlign() const { return matAlign_; }

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::VOLUMETEXTURE;
	};

	// scene 
	struct CORE_EXPORT RenderableComponent : ComponentBase
	{
	private:
		enum class RenderableFlags : uint32_t
		{
			EMPTY = 0,
			RENDERABLE = 1 << 0,
			REQUEST_PLANAR_REFLECTION = 1 << 4,
			LIGHTMAP_RENDER_REQUEST = 1 << 5,
			LIGHTMAP_DISABLE_BLOCK_COMPRESSION = 1 << 6,
			FOREGROUND = 1 << 7,
		};
		uint32_t flags_ = SCU32(RenderableFlags::EMPTY);

		uint8_t visibleLayerMask_ = 0x7;
		VUID vuidGeometry_ = INVALID_ENTITY;
		std::vector<VUID> vuidMaterials_;

		// parameters for visibility effect
		XMFLOAT3 visibleCenter_ = XMFLOAT3(0, 0, 0);
		float visibleRadius_ = 0;
		float fadeDistance_ = 0.f;

		// Non-serialized attributes:
		//	dirty check can be considered by the following components
		//		- transformComponent, geometryComponent, and material components (with their referencing textureComponents)
		bool isDirty_ = true;
		geometrics::AABB aabb_; // world AABB
	public:
		RenderableComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::RENDERABLE, entity, vuid) {}

		void SetDirty() { isDirty_ = true; }
		bool IsDirty() const { return isDirty_; }
		bool IsRenderable() const { return flags_ & SCU32(RenderableFlags::RENDERABLE); }

		void SetForeground(const bool enabled) { FLAG_SETTER(flags_, RenderableFlags::FOREGROUND) }
		bool IsForeground() const { return flags_ & SCU32(RenderableFlags::FOREGROUND); }

		void SetFadeDistance(const float fadeDistance) { fadeDistance_ = fadeDistance; }
		void SetVisibleRadius(const float radius) { visibleRadius_ = radius; }
		void SetVisibleCenter(const XMFLOAT3 center) { visibleCenter_ = center; }
		void SetGeometry(const Entity geometryEntity);
		void SetMaterial(const Entity materialEntity, const size_t slot);
		void SetMaterials(const std::vector<Entity>& materials);
		void SetVisibleMask(const uint8_t layerBits, const uint8_t maskBits) { SETVISIBLEMASK(visibleLayerMask_, layerBits, maskBits); timeStampSetter_ = TimerNow; }
		bool IsVisibleWith(uint8_t visibleLayerMask) const { return visibleLayerMask & visibleLayerMask_; }
		uint8_t GetVisibleMask() const { return visibleLayerMask_; }
		float GetFadeDistance() const { return fadeDistance_; }
		float GetVisibleRadius() const { return visibleRadius_; }
		XMFLOAT3 GetVisibleCenter() const { return visibleCenter_; }
		Entity GetGeometry() const;
		Entity GetMaterial(const size_t slot) const;
		std::vector<Entity> GetMaterials() const;
		void Update();
		geometrics::AABB GetAABB() const { return aabb_; }
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
		float intensity_ = 1.0f; // Brightness of light in. The units that this is defined in depend on the type of light. Point and spot lights use luminous intensity in candela (lm/sr) while directional lights use illuminance in lux (lm/m2). https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual

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
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		XMFLOAT3 direction = XMFLOAT3(0, 1, 0);
		XMFLOAT4 rotation = XMFLOAT4(0, 0, 0, 1);
		XMFLOAT3 scale = XMFLOAT3(1, 1, 1);
		
		inline void SetDirty() { isDirty_ = true; }
		inline bool IsDirty() const { return isDirty_; }
		inline void SetLightColor(XMFLOAT3 color) { color_ = color; timeStampSetter_ = TimerNow; }
		inline XMFLOAT3 GetLightColor() const { return color_; }
		inline float GetLightIntensity() const { return intensity_; }
		inline void SetRange(const float range) { range_ = range; isDirty_ = true; timeStampSetter_ = TimerNow; }
		inline float GetRange() const
		{
			float retval = range_;
			retval = std::max(0.001f, retval);
			retval = std::min(retval, 65504.0f); // clamp to 16-bit float max value
			return retval;
		}
		const geometrics::AABB& GetAABB() const { return aabb_; }
		inline LightType GetLightType() const { return type_; }
		inline void SetLightType(LightType type) { type_ = type; isDirty_ = true; timeStampSetter_ = TimerNow; };
		inline bool IsInactive() const { return intensity_ == 0 || range_ == 0; }

		inline void Update();	// if there is a transform entity, make sure the transform is updated!

		inline void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::LIGHT;
	};

	struct CORE_EXPORT CameraComponent : ComponentBase
	{
	public:
		enum class Projection : uint8_t
		{
			PERSPECTIVE,    //!< perspective projection, objects get smaller as they are farther
			ORTHO,           //!< orthonormal projection, preserves distances
			CUSTOM_PROJECTION
		};
	private:
		float zNearP_ = 0.1f;
		float zFarP_ = 5000.0f;
		float fovY_ = XM_PI / 3.0f;
		float focalLength_ = 1;
		float apertureSize_ = 0;
		XMFLOAT2 apertureShape_ = XMFLOAT2(1, 1);

		// These parameters are used differently depending on the projection mode.
		// 1. orthogonal : image plane's width and height
		// 2. perspective : computing aspect (W / H) ratio, i.e., (width_, height_) := (aspectRatio, 1.f)
		// NOTE: these are NOT buffer or texture resolution!
		float width_ = 0.0f;
		float height_ = 0.0f;

		uint8_t visibleLayerMask_ = ~0;
		Projection projectionType_ = Projection::PERSPECTIVE;

		// Non-serialized attributes:
		bool isDirty_ = true;
		XMFLOAT3 eye_ = XMFLOAT3(0, 0, 0);
		XMFLOAT3 at_ = XMFLOAT3(0, 0, 1);
		XMFLOAT3 up_ = XMFLOAT3(0, 1, 0);
		XMFLOAT3X3 rotationMatrix_ = math::IDENTITY_MATRIX33;
		XMFLOAT4X4 view_, projection_, viewProjection_;
		XMFLOAT4X4 invView_, invProjection_, invViewProjection_;
		vz::geometrics::Frustum frustum_ = {};

	public:
		CameraComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::CAMERA, entity, vuid) {
			view_ = projection_ = viewProjection_ = invView_ = invProjection_ = invViewProjection_ = math::IDENTITY_MATRIX;
		}

		// Non-serialized attributes:
		XMFLOAT2 jitter = XMFLOAT2(0, 0);

		void SetDirty() { isDirty_ = true; }
		bool IsDirty() const { return isDirty_; }
		uint8_t GetVisibleLayerMask() const { return visibleLayerMask_; }
		void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits) { SETVISIBLEMASK(visibleLayerMask_, layerBits, maskBits); }

		// consider TransformComponent and HierarchyComponent that belong to this CameraComponent entity
		bool SetWorldLookAtFromHierarchyTransforms();
		void SetWorldLookAt(const XMFLOAT3& eye, const XMFLOAT3& at, const XMFLOAT3& up) {
			eye_ = eye; at_ = at; up_ = up; isDirty_ = true;
			timeStampSetter_ = TimerNow;
		}
		void SetWorldLookTo(const XMFLOAT3& eye, const XMFLOAT3& view, const XMFLOAT3& up) {
			eye_ = eye; XMStoreFloat3(&at_, XMLoadFloat3(&eye) + XMLoadFloat3(&view)); up_ = up; isDirty_ = true;
			timeStampSetter_ = TimerNow;
		}
		void SetPerspective(float width, float height, float nearP, float farP, float fovY = XM_PI / 3.0f) {
			width_ = width; height_ = height; zNearP_ = nearP; zFarP_ = farP; fovY_ = fovY; 
			isDirty_ = true; timeStampSetter_ = TimerNow;
		}

		// update view matrix using camera extrinsics such as eye_, at_, and up_ set by the above setters
		// update proj matrix using camera intrinsics
		// update view-proj and their inverse matrices using the updated view and proj matrices
		void UpdateMatrix();

		const XMFLOAT3& GetWorldEye() const { return eye_; }
		const XMFLOAT3& GetWorldAt() const { return at_; }
		const XMFLOAT3& GetWorldUp() const { return up_; }
		const XMFLOAT3X3& GetWorldRotation() const { return rotationMatrix_; }
		const XMFLOAT4X4& GetView() const { return view_; }
		const XMFLOAT4X4& GetProjection() const { return projection_; }
		const XMFLOAT4X4& GetViewProjection() const { return viewProjection_; }
		const XMFLOAT4X4& GetInvView() const { return invView_; }
		const XMFLOAT4X4& GetInvProjection() const { return invProjection_; }
		const XMFLOAT4X4& GetInvViewProjection() const { return invViewProjection_; }
		const geometrics::Frustum& GetFrustum() const { return frustum_; }

		Projection GetProjectionType() const { return projectionType_; }
		float GetFovVertical() const { return fovY_; }
		float GetFocalLength() const { return focalLength_; }
		float GetApertureSize() const { return apertureSize_; }
		XMFLOAT2 GetApertureShape() const { return apertureShape_; }

		void GetWidthHeight(float* w, float* h) const { if (w) *w = width_; if (h) *h = height_; }
		void GetNearFar(float* n, float* f) const { if (n) *n = zNearP_; if (f) *f = zFarP_; }

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

	CORE_EXPORT inline size_t GetComponents(const Entity entity, std::vector<ComponentBase*>& components);
	CORE_EXPORT inline size_t GetEntitiesByName(const std::string& name, std::vector<Entity>& entities); // when there is a name component
	CORE_EXPORT Entity GetFirstEntityByName(const std::string& name);
}
