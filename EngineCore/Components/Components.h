#pragma once
#include "Libs/Math.h"
#include "Libs/PrimitiveHelper.h"

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
	inline static const std::string COMPONENT_INTERFACE_VERSION = "VZ::20241014";
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
		static bool DestoryScene(const Entity entity);

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

		primitive::AABB aabb_;

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

		inline std::string GetSceneName() { return name_; }
		inline Entity GetSceneEntity() { return entity_; }
		inline GScene* GetGSceneHandle() { return handlerScene_; }

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
		inline std::vector<Entity> GetGeometryEntities() const noexcept;
		inline std::vector<Entity> GetMaterialEntities() const noexcept;

		inline const primitive::AABB& GetAABB() const { return aabb_; }

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
		};
		enum class ShaderType : uint32_t
		{
			PHONG = 0,
			PBR,

			COUNT
		};
		enum class TextureSlot : uint32_t
		{
			BASECOLORMAP = 0,
			VOLUMEDENSITYMAP, // this is used for volume rendering

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

	private:
		uint32_t renderOptionFlags_ = (uint32_t)RenderFlags::FORWARD;
		ShaderType shaderType_ = ShaderType::PHONG;
		BlendMode blendMode_ = BlendMode::BLENDMODE_OPAQUE;

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

		inline const XMFLOAT4 GetBaseColor() const { return baseColor_; }	// w is opacity
		inline const XMFLOAT4 GetSpecularColor() const { return specularColor_; }
		inline const XMFLOAT4 GetEmissiveColor() const { return emissiveColor_; }	// w is emissive strength

		inline void SetBaseColor(const XMFLOAT4& baseColor) { baseColor_ = baseColor; isDirty_ = true; }
		inline void SetSpecularColor(const XMFLOAT4& specularColor) { specularColor_ = specularColor; isDirty_ = true; }
		inline void SetEmissiveColor(const XMFLOAT4& emissiveColor) { emissiveColor_ = emissiveColor; isDirty_ = true; }

		inline bool IsDirty() const { return isDirty_; }
		inline void SetDirty(const bool dirty) { isDirty_ = dirty; }
		inline bool IsOutlineEnabled() const { return renderOptionFlags_ & SCU32(RenderFlags::OUTLINE); }
		inline bool IsDoubleSided() const { return renderOptionFlags_ & SCU32(RenderFlags::DOUBLE_SIDED); }
		inline bool IsTesellated() const { return renderOptionFlags_ & SCU32(RenderFlags::TESSELATION); }
		inline bool IsAlphaTestEnabled() const { return renderOptionFlags_ & SCU32(RenderFlags::ALPHA_TEST); }
		inline BlendMode GetBlendMode() const { return blendMode_; }
		inline VUID GetTextureVUID(const size_t slot) const { 
			if (slot >= SCU32(TextureSlot::TEXTURESLOT_COUNT)) return INVALID_VUID; return textureComponents_[slot];
		}
		inline const XMFLOAT4 GetTexMulAdd() const { return texMulAdd_; }
		inline ShaderType GetShaderType() const { return shaderType_; }

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::MATERIAL;
	};

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

		struct Primitive {
		private:
			inline static const size_t NUMBUFFERS = 6;
			bool isValid_[NUMBUFFERS] = { }; // false

			std::vector<XMFLOAT3> vertexPositions_;
			std::vector<XMFLOAT3> vertexNormals_;
			std::vector<XMFLOAT2> vertexUVset0_;
			std::vector<XMFLOAT2> vertexUVset1_;
			std::vector<uint32_t> vertexColors_;
			std::vector<uint32_t> indexPrimitives_;

			primitive::AABB aabb_;
			PrimitiveType ptype_ = PrimitiveType::TRIANGLES;
		public:
			void MoveFrom(Primitive& primitive)
			{
				vertexPositions_ = std::move(primitive.vertexPositions_);
				if (vertexPositions_.size() > 0) isValid_[0] = true;
				vertexNormals_ = std::move(primitive.vertexNormals_);
				if (vertexNormals_.size() > 0) isValid_[1] = true;
				vertexUVset0_ = std::move(primitive.vertexUVset0_);
				if (vertexUVset0_.size() > 0) isValid_[2] = true;
				vertexUVset1_ = std::move(primitive.vertexUVset1_);
				if (vertexUVset1_.size() > 0) isValid_[3] = true;
				vertexColors_ = std::move(primitive.vertexColors_);
				if (vertexColors_.size() > 0) isValid_[4] = true;
				indexPrimitives_ = std::move(primitive.indexPrimitives_);
				if (indexPrimitives_.size() > 0) isValid_[5] = true;
				aabb_ = primitive.aabb_;
				ptype_ = primitive.ptype_;
			}
			void MoveTo(Primitive& primitive)
			{
				primitive.vertexPositions_ = std::move(vertexPositions_);
				primitive.vertexNormals_ = std::move(vertexNormals_);
				primitive.vertexUVset0_ = std::move(vertexUVset0_);
				primitive.vertexUVset1_ = std::move(vertexUVset1_);
				primitive.vertexColors_ = std::move(vertexColors_);
				primitive.indexPrimitives_ = std::move(indexPrimitives_);
				primitive.aabb_ = aabb_;
				primitive.ptype_ = ptype_;
				for (size_t i = 0; i < NUMBUFFERS; ++i) isValid_[i] = false;
			}
			primitive::AABB GetAABB() { return aabb_; }
			PrimitiveType GetPrimitiveType() { return ptype_; }
			bool IsValid() {
				return vertexPositions_.size() > 0 && aabb_.IsValid();
			}
			void SetAABB(const primitive::AABB& aabb) { aabb_ = aabb; }
			void SetPrimitiveType(const PrimitiveType ptype) { ptype_ = ptype; }
#define PRIM_GETTER(A)  if (data) { *data = A.data(); } return A.size();
			size_t GetVtxPositions(XMFLOAT3** data) { assert(isValid_[0]); PRIM_GETTER(vertexPositions_) }
			size_t GetVtxNormals(XMFLOAT3** data) { assert(isValid_[1]); PRIM_GETTER(vertexNormals_) }
			size_t GetVtxUVSet0(XMFLOAT2** data) { assert(isValid_[2]); PRIM_GETTER(vertexUVset0_) }
			size_t GetVtxUVSet1(XMFLOAT2** data) { assert(isValid_[3]); PRIM_GETTER(vertexUVset1_) }
			size_t GetVtxColors(uint32_t** data) { assert(isValid_[4]); PRIM_GETTER(vertexColors_) }
			size_t GetIdxPrimives(uint32_t** data) { assert(isValid_[5]); PRIM_GETTER(indexPrimitives_) }
			size_t GetNumVertices() const { return vertexPositions_.size(); }
			size_t GetNumIndices() const { return indexPrimitives_.size(); }

#define PRIM_SETTER(A, B) A##_ = onlyMoveOwnership ? std::move(A) : A; isValid_[B] = true;
			// move or copy
			// note: if onlyMoveOwnership is true, input std::vector will be invalid!
			//	carefully use onlyMoveOwnership(true) to avoid the ABI issue
			void SetVtxPositions(std::vector<XMFLOAT3>& vertexPositions, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexPositions, 0) }
			void SetVtxNormals(std::vector<XMFLOAT3>& vertexNormals, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexNormals, 1) }
			void SetVtxUVSet0(std::vector<XMFLOAT2>& vertexUVset0, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexUVset0, 2) }
			void SetVtxUVSet1(std::vector<XMFLOAT2>& vertexUVset1, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexUVset1, 3) }
			void SetVtxColors(std::vector<uint32_t>& vertexColors, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexColors, 4) }
			void SetIdxPrimives(std::vector<uint32_t>& indexPrimitives, const bool onlyMoveOwnership = false) { PRIM_SETTER(indexPrimitives, 5) }

			void Serialize(vz::Archive& archive, const uint64_t version);
		};
	private:
		std::vector<Primitive> parts_;	

		// Non-serialized attributes
		bool isDirty_ = true;
		primitive::AABB aabb_; // not serialized (automatically updated)
		bool hasBVH_ = false;

		void updateAABB();
	public:
		GeometryComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::GEOMETRY, entity, vuid) {}

		bool IsDirty() { return isDirty_; }
		primitive::AABB GetAABB() { return aabb_; }
		void MovePrimitives(std::vector<Primitive>& primitives);
		void CopyPrimitives(const std::vector<Primitive>& primitives);
		void MovePrimitive(Primitive& primitive, const size_t slot);
		void CopyPrimitive(const Primitive& primitive, const size_t slot);
		const Primitive* GetPrimitive(const size_t slot) const;
		const std::vector<Primitive>& GetPrimitives() const { return parts_; }
		size_t GetNumParts() const { return parts_.size(); }
		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::GEOMETRY;
	};

	struct Resource;
	constexpr size_t TEXTURE_MAX_RESOLUTION = 4096;
	struct CORE_EXPORT TextureComponent : ComponentBase
	{
	public:
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
		std::shared_ptr<Resource> internalResource_;
		uint32_t width_ = 1;
		uint32_t height_ = 1;
		uint32_t depth_ = 1;
		uint32_t arraySize_ = 1;

		// dicom info : using geometry or not
		VUID vuidBindingGeometry_ = INVALID_VUID;

		// sampler 
	public:
		TextureComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::TEXTURE, entity, vuid) {}
		
		TextureType GetTextureType() const { return textureType_; }
		bool IsValid() const;

		const std::vector<uint8_t>& GetData() const;
		int GetFontStyle() const;
		void CopyFromData(const std::vector<uint8_t>& data);
		void MoveFromData(std::vector<uint8_t>&& data);
		void SetOutdated();

		inline uint32_t GetWidth() const { return width_; }
		inline uint32_t GetHeight() const { return height_; }
		inline uint32_t GetDepth() const { return depth_; }
		inline uint32_t GetArraySize() const { return arraySize_; }
		inline void SetTextureDimension(const uint32_t w, const uint32_t h, const uint32_t d, const uint32_t array) {
			width_ = w, height_ = h, depth_ = d, arraySize_ = array;
		}
		
		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::TEXTURE;
	};

	// scene 
	struct CORE_EXPORT RenderableComponent : ComponentBase
	{
	private:
		uint8_t visibleLayerMask_ = 0x7;
		VUID vuidGeometry_ = INVALID_ENTITY;
		std::vector<VUID> vuidMaterials_;
		//VUID vuidWetmapTexture_ = INVALID_VUID;

		// Non-serialized attributes:
		//	dirty check can be considered by the following components
		//		- transformComponent, geometryComponent, and material components (with their referencing textureComponents)
		bool isValid_ = false;
		bool isDirty_ = true;
		primitive::AABB aabb_; // world AABB
	public:
		RenderableComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::RENDERABLE, entity, vuid) {}

		void SetDirty() { isDirty_ = true; }
		bool IsDirty() const { return isDirty_; }
		bool IsValid() const { return isValid_; }
		void SetGeometry(const Entity geometryEntity);
		void SetMaterial(const Entity materialEntity, const size_t slot);
		void SetMaterials(const std::vector<Entity>& materials);
		void SetVisibleMask(const uint8_t layerBits, const uint8_t maskBits) { SETVISIBLEMASK(visibleLayerMask_, layerBits, maskBits); timeStampSetter_ = TimerNow; }
		bool IsVisibleWith(uint8_t visibleLayerMask) const { return visibleLayerMask & visibleLayerMask_; }
		uint8_t GetVisibleMask() const { return visibleLayerMask_; }
		Entity GetGeometry() const;
		Entity GetMaterial(const size_t slot);
		std::vector<Entity> GetMaterials() const;
		void Update();
		primitive::AABB GetAABB() const { return aabb_; }
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

		primitive::AABB aabb_;

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
		primitive::AABB GetAABB() const { return aabb_; }
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
		vz::primitive::Frustum frustum_ = {};

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

		XMFLOAT3 GetWorldEye() const { return eye_; }
		XMFLOAT3 GetWorldAt() const { return at_; }
		XMFLOAT3 GetWorldUp() const { return up_; }
		XMFLOAT3X3 GetWorldRotation() const { return rotationMatrix_; }
		XMFLOAT4X4 GetView() const { return view_; }
		XMFLOAT4X4 GetProjection() const { return projection_; }
		XMFLOAT4X4 GetViewProjection() const { return viewProjection_; }
		XMFLOAT4X4 GetInvView() const { return invView_; }
		XMFLOAT4X4 GetInvProjection() const { return invProjection_; }
		XMFLOAT4X4 GetInvViewProjection() const { return invViewProjection_; }
		vz::primitive::Frustum GetFrustum() const { return frustum_; }

		Projection GetProjectionType() const { return projectionType_; }
		float GetFovVertical() const { return fovY_; }
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
	
	// TODO: Destory Group 
	// 1. Entity
	// 2. VUID
	// 3. ComponentBase
	// 4. Scene
	// 5. ??

	// here, inlining is actually applied only when building the same object file
	// calling in other built object files ignores the inlining

	// VUID Manager
	extern "C" CORE_EXPORT inline ComponentBase* GetComponentByVUID(const VUID vuid);
	extern "C" CORE_EXPORT inline Entity GetEntityByVUID(const VUID vuid);

	// Component Manager
	extern "C" CORE_EXPORT inline size_t SetSceneComponentsDirty(const Entity entity);

	extern "C" CORE_EXPORT inline NameComponent* CreateNameComponent(const Entity entity, const std::string& name);
	extern "C" CORE_EXPORT inline TransformComponent* CreateTransformComponent(const Entity entity);
	extern "C" CORE_EXPORT inline HierarchyComponent* CreateHierarchyComponent(const Entity entity, const Entity parent = INVALID_ENTITY);
	extern "C" CORE_EXPORT inline MaterialComponent* CreateMaterialComponent(const Entity entity);
	extern "C" CORE_EXPORT inline GeometryComponent* CreateGeometryComponent(const Entity entity);
	extern "C" CORE_EXPORT inline TextureComponent* CreateTextureComponent(const Entity entity);
	extern "C" CORE_EXPORT inline LightComponent* CreateLightComponent(const Entity entity);
	extern "C" CORE_EXPORT inline CameraComponent* CreateCameraComponent(const Entity entity);
	extern "C" CORE_EXPORT inline RenderableComponent* CreateRenderableComponent(const Entity entity);

	extern "C" CORE_EXPORT inline NameComponent* GetNameComponent(const Entity entity);
	extern "C" CORE_EXPORT inline TransformComponent* GetTransformComponent(const Entity entity);
	extern "C" CORE_EXPORT inline HierarchyComponent* GetHierarchyComponent(const Entity entity);
	extern "C" CORE_EXPORT inline MaterialComponent* GetMaterialComponent(const Entity entity);
	extern "C" CORE_EXPORT inline GeometryComponent* GetGeometryComponent(const Entity entity);
	extern "C" CORE_EXPORT inline TextureComponent* GetTextureComponent(const Entity entity);
	extern "C" CORE_EXPORT inline RenderableComponent* GetRenderableComponent(const Entity entity);
	extern "C" CORE_EXPORT inline LightComponent* GetLightComponent(const Entity entity);
	extern "C" CORE_EXPORT inline CameraComponent* GetCameraComponent(const Entity entity);

	extern "C" CORE_EXPORT inline size_t GetTransformComponents(const std::vector<Entity>& entities, std::vector<TransformComponent*>& comps);
	extern "C" CORE_EXPORT inline size_t GetHierarchyComponents(const std::vector<Entity>& entities, std::vector<HierarchyComponent*>& comps);
	extern "C" CORE_EXPORT inline size_t GetMaterialComponents(const std::vector<Entity>& entities, std::vector<MaterialComponent*>& comps);
	extern "C" CORE_EXPORT inline size_t GetLightComponents(const std::vector<Entity>& entities, std::vector<LightComponent*>& comps);

	extern "C" CORE_EXPORT inline bool ContainNameComponent(const Entity entity);
	extern "C" CORE_EXPORT inline bool ContainTransformComponent(const Entity entity);
	extern "C" CORE_EXPORT inline bool ContainHierarchyComponent(const Entity entity);
	extern "C" CORE_EXPORT inline bool ContainMaterialComponent(const Entity entity);
	extern "C" CORE_EXPORT inline bool ContainGeometryComponent(const Entity entity);
	extern "C" CORE_EXPORT inline bool ContainRenderableComponent(const Entity entity);
	extern "C" CORE_EXPORT inline bool ContainLightComponent(const Entity entity);
	extern "C" CORE_EXPORT inline bool ContainCameraComponent(const Entity entity);

	extern "C" CORE_EXPORT inline size_t GetComponents(const Entity entity, std::vector<ComponentBase*>& components);
	extern "C" CORE_EXPORT inline size_t GetEntitiesByName(const std::string& name, std::vector<Entity>& entities); // when there is a name component
}
