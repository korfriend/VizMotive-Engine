#pragma once
#include "Utils/vzMath.h"
#include "Utils/Random.h"
#include "Utils/Geometrics.h"

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
using Entity = uint64_t;
using VUID = uint64_t;
inline constexpr Entity INVALID_ENTITY = 0;
inline constexpr VUID INVALID_VUID = 0;
using TimeStamp = std::chrono::high_resolution_clock::time_point;
#define TimeDurationCount(A, B) std::chrono::duration_cast<std::chrono::duration<double>>(A - B).count()
#define TimerNow std::chrono::high_resolution_clock::now()
#define TimerMin {} // indicating 1970/1/1 (00:00:00 UTC), DO NOT USE 'std::chrono::high_resolution_clock::time_point::min()'

namespace vz
{
	inline static const std::string COMPONENT_INTERFACE_VERSION = "VZ::20250804_0";
	CORE_EXPORT std::string GetComponentVersion();

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
	inline constexpr uint32_t CombineStencilrefs(StencilRef engineStencilRef, uint8_t userStencilRef)
	{
		return (userStencilRef << 4) | static_cast<uint8_t>(engineStencilRef);
	}

	class Archive;
	struct ComponentBase;
	struct CameraComponent;
	struct GScene;
	struct GRenderableComponent;
	struct GSpriteComponent;
	struct GSpriteFontComponent;
	struct GGeometryComponent;
	struct GMaterialComponent;
	struct GLightComponent;
	struct GProbeComponent;

	class WaitForBool {
	private:
		std::atomic<bool> flag; // if false, then wait!
		std::mutex mtx;
		std::condition_variable cv;

	public:
		WaitForBool() : flag(true) {}

		// Called from another thread to set the flag to true
		inline void setFree() {
			flag.store(true, std::memory_order_release);
			// Notify all waiting threads
			cv.notify_all();
		}

		// Wait until flag becomes true
		inline void waitForFree() {
			// Quick check before locking (optimization)
			if (flag.load(std::memory_order_acquire)) {
				return;
			}

			std::unique_lock<std::mutex> lock(mtx);
			// Wait until flag becomes true
			cv.wait(lock, [this]() {
				return flag.load(std::memory_order_acquire);
				});
			// At this point, flag is true
		}

		// Alternative wait method with timeout
		template<typename Rep, typename Period>
		inline bool waitForFree(const std::chrono::duration<Rep, Period>& timeout) {
			// Quick check before locking
			if (flag.load(std::memory_order_acquire)) {
				return true;
			}

			std::unique_lock<std::mutex> lock(mtx);
			// Wait until flag becomes true or timeout
			return cv.wait_for(lock, timeout, [this]() {
				return flag.load(std::memory_order_acquire);
				});
		}

		// Check current flag value without waiting
		inline bool isFree() const {
			return flag.load(std::memory_order_acquire);
		}

		// Reset flag to false
		inline void setWait() {
			flag.store(false, std::memory_order_release);
		}

		inline bool freeTestAndSetWait() {
			bool expected = true;
			return flag.compare_exchange_strong(expected, false);
		}
	};

	enum class RenderableFilterFlags
	{
		RENDERABLE_MESH_OPAQUE = 1 << 0,
		RENDERABLE_MESH_TRANSPARENT = 1 << 1,
		RENDERABLE_MESH_WATER = 1 << 2,
		RENDERABLE_MESH_NAVIGATION = 1 << 3,
		RENDERABLE_COLLIDER = 1 << 4,
		RENDERABLE_VOLUME_DVR = 1 << 5,
		// Include everything:
		RENDERABLE_ALL = ~0,
	};

	struct CORE_EXPORT Scene
	{
	protected:
		std::string name_;

		// Scene lights (Skybox or Weather something...)
		XMFLOAT3 ambient_ = XMFLOAT3(0.25f, 0.25f, 0.25f);

		// Instead of Entity, VUID is stored by serialization
		//	the index is same to the streaming index
		std::vector<Entity> transforms_;
		std::vector<Entity> renderables_;
		std::vector<Entity> lights_;
		std::vector<Entity> probes_;
		std::vector<Entity> cameras_;

		// TODO
		// camera for reflection 

		// -----------------------------------------
		// Non-serialized attributes:
		Entity entity_ = INVALID_ENTITY;
		std::unordered_map<Entity, uint32_t> lookupTransforms_;	// note: all entities have TransformComponent
		std::vector<Entity> children_;
		std::vector<Entity> materials_;
		std::vector<Entity> geometries_;
		std::vector<Entity> colliders_;

		geometrics::AABB aabb_;	// entire scene box (renderables, lights, ...)

		Entity environment_ = INVALID_ENTITY;
		
		// Non-serialized Attributes
		//	instant parameters during render-process
		float dt_ = 0.f;
		float deltaTimeAccumulator_ = 0.f;
		bool isContentChanged_ = true;	// since last recentUpdateTime_
		TimeStamp recentUpdateTime_ = TimerMin;	// world update time
		TimeStamp timeStampSetter_ = TimerMin;	// add or remove scene components

	public:
		Scene(const Entity entity, const std::string& name) : entity_(entity), name_(name) {}
		virtual ~Scene() = default;

		size_t stableCount = 0;
		float targetFrameRate = 60;
		bool frameskip = true; // just for fixed update (later for physics-based simulations)
		bool framerateLock = true;
		TimeStamp timerStamp = TimerNow; // scene update (same to ComponentBase::timeStampSetter_
		Entity cameraMain = INVALID_ENTITY; // Main camera for the scene; defaults to the renderer's first 3D camera (not slicer) if not set.

		double RecordElapsedSeconds() {
			TimeStamp timestamp2 = TimerNow;
			double duration = TimeDurationCount(timestamp2, timerStamp);
			timerStamp = timestamp2;
			return duration;
		}

		bool IsContentChanged() const { return isContentChanged_; }

		inline const std::string GetSceneName() const { return name_; }
		inline const Entity GetSceneEntity() const { return entity_; }

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
		inline size_t GetEntityCount() const noexcept { return lookupTransforms_.size(); }

		/**
		 * Returns the number of active (alive) Renderable components in the Scene.
		 *
		 * @return The number of active (alive) Renderable components in the Scene.
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
		inline const std::vector<Entity>& GetProbeEntities() const noexcept { return probes_; }
		inline const std::vector<Entity>& GetCameraEntities() const noexcept { return cameras_; }

		inline const std::vector<Entity>& GetChildrenEntities() const noexcept { return children_; }

		// requires scanning process
		inline std::vector<Entity> GetGeometryEntities() const noexcept { return geometries_; }
		inline std::vector<Entity> GetMaterialEntities() const noexcept { return materials_; }
		inline std::vector<Entity> GetColliderEntities() const noexcept { return colliders_; }

		inline const geometrics::AABB& GetAABB() const { return aabb_; }

		/**
		 * Returns true if the given entity is in the Scene.
		 *
		 * @return Whether the given entity is in the Scene.
		 */
		inline bool HasEntity(const Entity entity) const noexcept
		{
			return lookupTransforms_.count(entity) > 0;
		}

		inline size_t GetEntities(std::vector<Entity>& entities) const
		{
			entities.resize(lookupTransforms_.size());
			size_t count = 0;
			for (auto it = lookupTransforms_.begin(); it != lookupTransforms_.end(); it++)
			{
				entities[count++] = it->first;
			}
			return entities.size();
		}

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
			int triIndex = -1;

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
		RayIntersectionResult Intersects(const geometrics::Ray& ray, const Entity entityCamera,
			uint32_t filterMask = SCU32(RenderableFilterFlags::RENDERABLE_MESH_OPAQUE), 
			uint32_t layerMask = ~0, uint32_t lod = 0, float toleranceRadius = 0, int screenW = -1, int screenH = -1) const;

		// Temporal version of Environment interfaces
		inline void SetAmbient(const XMFLOAT3& ambient) { ambient_ = ambient; timeStampSetter_ = TimerNow; }
		inline XMFLOAT3 GetAmbient() const { return ambient_; }
		uint32_t mostImportantLightIndex = ~0u;
		virtual bool LoadIBL(const std::string& filename) = 0;
		const Entity GetEnvironment() const { return environment_; };
		void SetEnvironment(const Entity entityEnv) { environment_ = entityEnv; timeStampSetter_ = TimerNow; }

		/**
		 * Removes the Renderable from the Scene.
		 *
		 * @param entity The Entity to remove from the Scene. If the specified
		 *                   \p entity doesn't exist, this call is ignored.
		 */
		virtual void Remove(const Entity entity) = 0;
		virtual void Update(const float dt) = 0;
		virtual GScene* GetGSceneHandle() const = 0;
		virtual uint32_t GetRenderableMeshCount() const = 0;
		virtual uint32_t GetRenderableVolumeCount() const = 0;
		virtual uint32_t GetRenderableGSplatCount() const = 0;

		virtual const std::vector<XMFLOAT4X4>& GetRenderableWorldMatrices() const = 0;
		virtual const std::vector<XMFLOAT4X4>& GetRenderableWorldMatricesPrev() const = 0;

		virtual const std::vector<geometrics::AABB>& GetRenderableAABBs() const = 0;
		virtual const std::vector<geometrics::AABB>& GetLightAABBs() const = 0;
		virtual const std::vector<geometrics::AABB>& GetProbeAABBs() const = 0;

		virtual const std::vector<GRenderableComponent*>& GetRenderableComponents() const = 0;
		virtual const std::vector<GRenderableComponent*>& GetRenderableMeshComponents() const = 0;
		virtual const std::vector<GRenderableComponent*>& GetRenderableVolumeComponents() const = 0;
		virtual const std::vector<GRenderableComponent*>& GetRenderableGSplatComponents() const = 0;
		virtual const std::vector<GRenderableComponent*>& GetRenderableSpriteComponents() const = 0;
		virtual const std::vector<GRenderableComponent*>& GetRenderableSpritefontComponents() const = 0;
		virtual const std::vector<GGeometryComponent*>& GetGeometryComponents() const = 0;
		virtual const std::vector<GMaterialComponent*>& GetMaterialComponents() const = 0;
		virtual const std::vector<GLightComponent*>& GetLightComponents() const = 0;
		virtual const std::vector<GProbeComponent*>& GetProbeComponents() const = 0;
		virtual const std::vector<CameraComponent*>& GetCameraComponents() const = 0;

		virtual const uint32_t GetGeometryPrimitivesAllocatorSize() const = 0;
		virtual const uint32_t GetRenderableResLookupAllocatorSize() const = 0;

		virtual void Debug_AddLine(const XMFLOAT3 p0, const XMFLOAT3 p1, const XMFLOAT4 color0, const XMFLOAT4 color1, const bool depthTest) const = 0;
		virtual void Debug_AddPoint(const XMFLOAT3 p, const XMFLOAT4 color, const bool depthTest) const = 0;
		virtual void Debug_AddCircle(const XMFLOAT3 p, const float r, const XMFLOAT4 color, const bool depthTest) const = 0;

		/**
		 * Read/write scene components (renderables, lights and Scene-attached cameras), make sure their VUID-based components are serialized first
		 */
		inline void Serialize(vz::Archive& archive);
	};

	enum class ComponentType : uint8_t
	{
		UNDEFINED = 0,
		NAME,
		TRANSFORM,
		LAYERDMASK,
		HIERARCHY,
		MATERIAL,
		GEOMETRY,
		RENDERABLE,
		SPRITE,
		SPRITEFONT,
		COLLIDER,
		TEXTURE,
		VOLUMETEXTURE,
		LIGHT,
		CAMERA,
		SLICER,
		ENVIRONMENT,
		PROBE,
	};

	struct CORE_EXPORT ComponentBase
	{
	protected:
		// global serialized attributes
		ComponentType cType_ = ComponentType::UNDEFINED;
		VUID vuid_ = INVALID_VUID;

		// Non-serialized attributes:
		TimeStamp timeStampSetter_ = TimerMin;
		Entity entity_ = INVALID_ENTITY;
		std::shared_ptr<std::mutex> mutex_ = std::make_shared<std::mutex>();

	public:
		ComponentBase() = default;
		ComponentBase(const ComponentType compType, const Entity entity, const VUID vuid);
		virtual ~ComponentBase() = default;

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
		virtual ~NameComponent() = default;

		inline void SetName(const std::string& name);
		inline const std::string& GetName() const { return name_; }

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
		virtual ~TransformComponent() = default;

		inline bool IsDirty() const { return isDirty_; }
		inline bool IsMatrixAutoUpdate() const { return isMatrixAutoUpdate_; }
		inline void SetMatrixAutoUpdate(const bool enable) { isMatrixAutoUpdate_ = enable; timeStampSetter_ = TimerNow; }

		// recommend checking IsDirtyWorldMatrix with scene's timeStampWorldUpdate
		inline const XMFLOAT3 GetWorldPosition() const;
		inline const XMFLOAT4 GetWorldRotation() const;
		inline const XMFLOAT3 GetWorldScale() const; 
		inline const XMFLOAT3 GetWorldForward() const;	// (-)z axis
		inline const XMFLOAT3 GetWorldUp() const;		// y axis
		inline const XMFLOAT3 GetWorldRight() const;	// x axis
		inline const XMFLOAT4X4& GetWorldMatrix() const { return world_; };

		// Local
		inline const XMFLOAT3& GetPosition() const { return position_; };
		inline const XMFLOAT4& GetRotation() const { return rotation_; };
		inline const XMFLOAT3& GetScale() const { return scale_; };
		inline const XMFLOAT4X4& GetLocalMatrix() const { return local_; };

		inline void SetPosition(const XMFLOAT3& p) { isDirty_ = true; position_ = p; timeStampSetter_ = TimerNow; }
		inline void SetScale(const XMFLOAT3& s) { isDirty_ = true; scale_ = s; timeStampSetter_ = TimerNow; }
		inline void SetEulerAngleRPY(const XMFLOAT3& rotAngles) { SetEulerAngleZXY({ -rotAngles.z, rotAngles.x, rotAngles.y }); }
		inline void SetEulerAngleZXY(const XMFLOAT3& rotAngles); // ROLL(-z)->PITCH(x)->YAW(y) (mainly used CG-convention) 
		inline void SetEulerAngleRPYInDegree(const XMFLOAT3& rotAngles) { SetEulerAngleZXYInDegree({ -rotAngles.z, rotAngles.x, rotAngles.y }); }
		inline void SetEulerAngleZXYInDegree(const XMFLOAT3& rotAngles); // ROLL(-z)->PITCH(x)->YAW(y) (mainly used CG-convention) 
		inline void SetQuaternion(const XMFLOAT4& q) { isDirty_ = true; rotation_ = q; timeStampSetter_ = TimerNow; }
		inline void SetRotateAxis(const XMFLOAT3& axis, const float rotAngle);
		inline void SetMatrix(const XMFLOAT4X4& local);
		inline void SetWorldMatrix(const XMFLOAT4X4& world);

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
		virtual ~HierarchyComponent() = default;

		inline void SetParentByVUID(const VUID vuidParent);
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

	struct CORE_EXPORT LayeredMaskComponent : ComponentBase
	{
	protected:
		// final visibility determined by bitwise AND with target visibleLayerMask_ (e.g., CameraComponent::visibleLayerMask_)
		uint32_t visibleLayerMask_ = 0x1;	// will be checked against each CameraComponent's layer using bitwise AND
		uint32_t stencilLayerMask_ = ~0u;
		uint32_t userLayerMask_ = 0u;

	public:
		LayeredMaskComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::LAYERDMASK, entity, vuid) {}
		virtual ~LayeredMaskComponent() = default;

		inline void SetVisibleLayerMask(const uint32_t visibleLayerMask) { visibleLayerMask_ = visibleLayerMask; timeStampSetter_ = TimerNow; }
		inline bool IsVisibleWith(uint32_t layerBits) const { return layerBits & visibleLayerMask_; }
		inline uint32_t GetVisibleLayerMask() const { return visibleLayerMask_; }

		inline void SetStencilLayerMask(const uint32_t stencilLayerMask) { stencilLayerMask_ = stencilLayerMask_; timeStampSetter_ = TimerNow; }
		inline uint32_t GetStencilLayerMask() const { return stencilLayerMask_; }

		inline void SetUserLayerMask(const uint32_t userLayerMask) { userLayerMask_ = userLayerMask; timeStampSetter_ = TimerNow; }
		inline uint32_t GetUserLayerMask() const { return userLayerMask_; }

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::LAYERDMASK;
	};

#define FLAG_SETTER(FLAG, FLAG_ENUM) value ? FLAG |= SCU32(FLAG_ENUM) : FLAG &= ~SCU32(FLAG_ENUM);
#define UNFLAG_SETTER(FLAG, FLAG_ENUM) !value ? FLAG |= SCU32(FLAG_ENUM) : FLAG &= ~SCU32(FLAG_ENUM);

	// resources
	struct CORE_EXPORT MaterialComponent : ComponentBase
	{
	public:
		enum class RenderFlags : uint32_t
		{
			USE_VERTEXCOLORS = 1 << 0,
			DOUBLE_SIDED = 1 << 1, // Forced OPAQUENESS 
			OUTLINE = 1 << 2,
			WIREFRAME = 1 << 3,
			TRANSPARENCY = 1 << 4, // "not forward" refers to "deferred"
			TESSELATION = 1 << 5,
			ALPHA_TEST = 1 << 6,
			WETMAP = 1 << 7,
			SHADOW_CAST = 1 << 8,
			SHADOW_RECEIVE = 1 << 9,
			VERTEXAO = 1 << 10,
			SPECULAR_GLOSSINESS_WORKFLOW = 1 << 11,
			OCCLUSION_PRIMARY = 1 << 12,
			OCCLUSION_SECONDARY = 1 << 13,
			USE_WIND = 1 << 14,
			DISABLE_CAPSULE_SHADOW = 1 << 15,	// default is CAPSULE SHADOW!
			GAUSSIAN_SPLATTING = 1 << 16,
		};
		enum class ShaderType : uint32_t
		{
			PHONG = 0,
			PBR,
			//PBR_ANISOTROPIC,
			UNLIT,
			VOLUMEMAP,

			//WATER,

			COUNT	// UPDATE ShaderInterop.h's SHADERTYPE_BIN_COUNT when modifying ShaderType elements
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

	protected:
		uint32_t flags_ = 0u;
		ShaderType shaderType_ = ShaderType::PHONG;
		BlendMode blendMode_ = BlendMode::BLENDMODE_OPAQUE;
		StencilRef engineStencilRef_ = StencilRef::STENCILREF_DEFAULT;
		uint8_t userStencilRef_ = 0;

		XMFLOAT4 baseColor_ = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 specularColor_ = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 emissiveColor_ = XMFLOAT4(1, 1, 1, 0);
		XMFLOAT4 sheenColor_ = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 subsurfaceScattering_ = XMFLOAT4(1, 1, 1, 0);
		XMFLOAT4 extinctionColor_ = XMFLOAT4(0, 0.9f, 1, 1);
		XMFLOAT4 phongFactors_ = XMFLOAT4(0.2f, 1, 1, 1);	// only used for ShaderType::PHONG

		float alphaRef_ = 1.f;
		float sheenRoughness_ = 0;
		float clearcoat_ = 0;
		float clearcoatRoughness_ = 0;
		float reflectance_ = 0.02f;
		float refraction_ = 0.0f;
		float normalMapStrength_ = 1.0f;
		float parallaxOcclusionMapping_ = 0.0f;
		float displacementMapping_ = 0.0f;
		float transmission_ = 0.0f;
		float anisotropyStrength_ = 0;
		float anisotropyRotation_ = 0; //radians, counter-clockwise
		float blendWithTerrainHeight_ = 0;
		float cloak_ = 0;
		float chromaticAberration_ = 0;

		float metalness_ = 0.f; 
		float roughness_ = 0.2f; 
		float saturation_ = 1.f;

		bool wireframe_ = false;

		VUID vuidTextureComponents_[SCU32(TextureSlot::TEXTURESLOT_COUNT)] = {};
		VUID vuidVolumeTextureComponents_[SCU32(VolumeTextureSlot::VOLUME_TEXTURESLOT_COUNT)] = {};
		VUID vuidLookupTextureComponents_[SCU32(LookupTableSlot::LOOKUPTABLE_COUNT)] = {};

		XMFLOAT4 texMulAdd_ = XMFLOAT4(1, 1, 0, 0); // dynamic multiplier (.xy) and addition (.zw) for UV coordinates

		VUID vuidVolumeMapperRenderable_ = INVALID_VUID;
		VolumeTextureSlot volumemapperVolumeSlot_ = VolumeTextureSlot::VOLUME_MAIN_MAP;
		LookupTableSlot volumemapperLookupSlot_ = LookupTableSlot::LOOKUP_COLOR;

		// Non-serialized Attributes:
	public:
		MaterialComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::MATERIAL, entity, vuid) {}
		virtual ~MaterialComponent() = default;

		inline void SetShaderType(const ShaderType shaderType) { shaderType_ = shaderType; timeStampSetter_ = TimerNow; }
		inline ShaderType GetShaderType() const { return shaderType_; }

		inline const float GetOpacity() const { return baseColor_.w; }
		inline const XMFLOAT4& GetBaseColor() const { return baseColor_; }	// w is opacity
		inline const XMFLOAT4& GetSpecularColor() const { return specularColor_; }
		inline const XMFLOAT4& GetEmissiveColor() const { return emissiveColor_; }	// w is emissive strength

		inline void SetAlphaRef(const float alphaRef) { alphaRef_ = alphaRef; timeStampSetter_ = TimerNow; }
		inline void SetSaturate(const float saturate) { saturation_ = saturate; timeStampSetter_ = TimerNow; }
		inline void SetBaseColor(const XMFLOAT4& baseColor) { baseColor_ = baseColor; timeStampSetter_ = TimerNow; }
		inline void SetSpecularColor(const XMFLOAT4& specularColor) { specularColor_ = specularColor; timeStampSetter_ = TimerNow;}
		inline void SetEmissiveColor(const XMFLOAT4& emissiveColor) { emissiveColor_ = emissiveColor; timeStampSetter_ = TimerNow;}
		inline void SetMatalness(const float metalness) { metalness_ = metalness; timeStampSetter_ = TimerNow; }
		inline void SetRoughness(const float roughness) { roughness_ = roughness; timeStampSetter_ = TimerNow; }
		inline void SetWetmapEnabled(const bool value) { FLAG_SETTER(flags_, RenderFlags::WETMAP); timeStampSetter_ = TimerNow; }
		inline void SetShadowCastEnabled(const bool value) { FLAG_SETTER(flags_, RenderFlags::SHADOW_CAST); timeStampSetter_ = TimerNow; }
		inline void SetShadowReceiveEnabled(const bool value) { FLAG_SETTER(flags_, RenderFlags::SHADOW_RECEIVE); timeStampSetter_ = TimerNow; }
		inline void SetDoubleSidedEnabled(const bool value) { FLAG_SETTER(flags_, RenderFlags::DOUBLE_SIDED); timeStampSetter_ = TimerNow; }
		inline void SetGaussianSplattingEnabled(const bool value) { FLAG_SETTER(flags_, RenderFlags::GAUSSIAN_SPLATTING); timeStampSetter_ = TimerNow; }
		inline void SetWireframeEnabled(const bool value) { FLAG_SETTER(flags_, RenderFlags::WIREFRAME); timeStampSetter_ = TimerNow; }
		inline void SetUseVertexColorEnabled(const bool value) { FLAG_SETTER(flags_, RenderFlags::USE_VERTEXCOLORS); timeStampSetter_ = TimerNow; }
		inline void SetUseSpecularGlossinessWorkflowEnabled(const bool value) { FLAG_SETTER(flags_, RenderFlags::USE_VERTEXCOLORS); timeStampSetter_ = TimerNow; }
		inline void SetPhongFactors(const XMFLOAT4& phongFactors) { phongFactors_ = phongFactors; timeStampSetter_ = TimerNow; }
		inline void SetOcclusionEnabled_Primary(const bool value) { FLAG_SETTER(flags_, RenderFlags::OCCLUSION_PRIMARY); timeStampSetter_ = TimerNow; }
		inline void SetOcclusionEnabled_Secondary(const bool value) { FLAG_SETTER(flags_, RenderFlags::OCCLUSION_SECONDARY); timeStampSetter_ = TimerNow; }
		inline void SetUseWindEnabled(const bool value) { FLAG_SETTER(flags_, RenderFlags::USE_WIND); timeStampSetter_ = TimerNow; }
		inline void SetCapsuleShadowEnabled(const bool value) { UNFLAG_SETTER(flags_, RenderFlags::DISABLE_CAPSULE_SHADOW); timeStampSetter_ = TimerNow; }
		
		inline void SetSheenColor(const XMFLOAT4& value) { sheenColor_ = value; timeStampSetter_ = TimerNow; }
		inline void SetSubsurfaceScattering(const XMFLOAT4& value) { subsurfaceScattering_ = value; timeStampSetter_ = TimerNow; }
		inline void SetExtinctionColor(const XMFLOAT4& value) { extinctionColor_ = value; timeStampSetter_ = TimerNow; }
		inline void SetSheenRoughness(const float value) { sheenRoughness_ = value; timeStampSetter_ = TimerNow; }
		inline void SetClearcoat(const float value) { clearcoat_ = value; timeStampSetter_ = TimerNow; }
		inline void SetClearcoatRoughness(const float value) { clearcoatRoughness_ = value; timeStampSetter_ = TimerNow; }
		inline void SetReflectance(const float value) { reflectance_ = value; timeStampSetter_ = TimerNow; }
		inline void SetRefraction(const float value) { refraction_ = value; timeStampSetter_ = TimerNow; }
		inline void SetNormalMapStrength(const float value) { normalMapStrength_ = value; timeStampSetter_ = TimerNow; }
		inline void SetParallaxOcclusionMapping(const float value) { parallaxOcclusionMapping_ = value; timeStampSetter_ = TimerNow; }
		inline void SetDisplacementMapping(const float value) { displacementMapping_ = value; timeStampSetter_ = TimerNow; }
		inline void SetTransmission(const float value) { transmission_ = value; timeStampSetter_ = TimerNow; }
		inline void SetAnisotropyStrength(const float value) { anisotropyStrength_ = value; timeStampSetter_ = TimerNow; }
		inline void SetAnisotropyRotation(const float value) { anisotropyRotation_ = value; timeStampSetter_ = TimerNow; }
		inline void SetBlendWithTerrainHeight(const float value) { blendWithTerrainHeight_ = value; timeStampSetter_ = TimerNow; }
		inline void SetCloak(const float value) { cloak_ = value; timeStampSetter_ = TimerNow; }
		inline void SetChromaticAberration(const float value) { chromaticAberration_ = value; timeStampSetter_ = TimerNow; }

		inline void SetTexture(const Entity textureEntity, const TextureSlot textureSlot);
		inline void SetVolumeTexture(const Entity volumetextureEntity, const VolumeTextureSlot volumetextureSlot);
		inline void SetLookupTable(const Entity lookuptextureEntity, const LookupTableSlot lookuptextureSlot);

		inline void SetVolumeMapper(const Entity targetRenderableEntity, const VolumeTextureSlot volumetextureSlot = VolumeTextureSlot::VOLUME_MAIN_MAP, const LookupTableSlot lookupSlot = LookupTableSlot::LOOKUP_COLOR);
		inline VUID GetVolumeMapperTargetRenderableVUID() const { return vuidVolumeMapperRenderable_; }
		inline VolumeTextureSlot GetVolumeMapperVolumeSlot() const { return volumemapperVolumeSlot_; }
		inline LookupTableSlot GetVolumeMapperLookupSlot() const { return volumemapperLookupSlot_; }

		inline bool IsOutlineEnabled() const { return flags_ & SCU32(RenderFlags::OUTLINE); }
		inline bool IsDoubleSided() const { return flags_ & SCU32(RenderFlags::DOUBLE_SIDED); }
		inline bool IsTesellated() const { return flags_ & SCU32(RenderFlags::TESSELATION); }
		inline bool IsAlphaTestEnabled() const { return flags_ & SCU32(RenderFlags::ALPHA_TEST); }
		inline bool IsWetmapEnabled() const { return flags_ & SCU32(RenderFlags::WETMAP); }
		inline bool IsShadowCast() const { return flags_ & SCU32(RenderFlags::SHADOW_CAST); }
		inline bool IsShadowReceive() const { return flags_ & SCU32(RenderFlags::SHADOW_RECEIVE); }
		inline bool IsVertexAOEnabled() const { return flags_ & SCU32(RenderFlags::VERTEXAO); }
		inline bool IsGaussianSplattingEnabled() const { return flags_ & SCU32(RenderFlags::GAUSSIAN_SPLATTING); }
		inline bool IsWireframeEnabled() const { return flags_ & SCU32(RenderFlags::WIREFRAME); }
		inline bool IsUsingVertexColor() const { return flags_ & SCU32(RenderFlags::USE_VERTEXCOLORS); }
		inline bool IsUsingSpecularGlossinessWorkflow() const { return flags_ & SCU32(RenderFlags::SPECULAR_GLOSSINESS_WORKFLOW); }
		inline bool IsOcclusionEnabled_Primary() const { return flags_ & SCU32(RenderFlags::OCCLUSION_PRIMARY); }
		inline bool IsOcclusionEnabled_Secondary() const { return flags_ & SCU32(RenderFlags::OCCLUSION_SECONDARY); }
		inline bool IsUsingWind() const { return flags_ & SCU32(RenderFlags::USE_WIND); }
		inline bool IsCapsuleShadowEnabled() const { return flags_ & ~SCU32(RenderFlags::DISABLE_CAPSULE_SHADOW); }

		inline uint32_t GetRenderFlags() const { return flags_; }

		inline float GetAlphaRef() const { return alphaRef_; }
		inline float GetSaturate() const { return saturation_; }
		inline float GetMatalness() const { return metalness_; }
		inline float GetRoughness() const { return roughness_; }
		inline BlendMode GetBlendMode() const { return blendMode_; }
		inline VUID GetTextureVUID(const TextureSlot slot) const { return vuidTextureComponents_[SCU32(slot)]; }
		inline VUID GetVolumeTextureVUID(const VolumeTextureSlot slot) const { return vuidVolumeTextureComponents_[SCU32(slot)]; }
		inline VUID GetLookupTableVUID(const LookupTableSlot slot) const { return vuidLookupTextureComponents_[SCU32(slot)]; }
		inline XMFLOAT4 GetTexMulAdd() const { return texMulAdd_; }
		inline XMFLOAT4 GetPhongFactors() const { return phongFactors_; }

		inline XMFLOAT4 GetSheenColor() const { return sheenColor_; }
		inline XMFLOAT4 GetSubsurfaceScattering() const { return subsurfaceScattering_; }
		inline XMFLOAT4 GetExtinctionColor() const { return extinctionColor_; }
		inline float GetSheenRoughness() const { return sheenRoughness_; }
		inline float GetClearcoat() const { return clearcoat_; }
		inline float GetClearcoatRoughness() const { return clearcoatRoughness_; }
		inline float GetReflectance() const { return reflectance_; }
		inline float GetRefraction() const { return refraction_; }
		inline float GetNormalMapStrength() const { return normalMapStrength_; }
		inline float GetParallaxOcclusionMapping() const { return parallaxOcclusionMapping_; }
		inline float GetDisplacementMapping() const { return displacementMapping_; }
		inline float GetTransmission() const { return transmission_; }
		inline float GetAnisotropyStrength() const { return anisotropyStrength_; }
		inline float GetAnisotropyRotation() const { return anisotropyRotation_; }
		inline float GetBlendWithTerrainHeight() const { return blendWithTerrainHeight_; }
		inline float GetCloak() const { return cloak_; }
		inline float GetChromaticAberration() const { return chromaticAberration_; }

		inline void SetStencilRef(StencilRef value) { engineStencilRef_ = value; }
		inline StencilRef GetStencilRef() const { return engineStencilRef_; }
		// User stencil value can be in range [0, 15]
		inline void SetUserStencilRef(uint8_t value) { userStencilRef_ = value & 0x0F; }
		uint32_t GetUserStencilRef() const { return CombineStencilrefs(engineStencilRef_, userStencilRef_); }

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

		struct Primitive {
		private:
			std::vector<XMFLOAT3> vertexPositions_;
			std::vector<XMFLOAT3> vertexNormals_;
			std::vector<XMFLOAT4> vertexTangents_;
			std::vector<XMFLOAT2> vertexUVset0_;
			std::vector<XMFLOAT2> vertexUVset1_;
			std::vector<uint32_t> vertexColors_;
			std::vector<uint32_t> indexPrimitives_;

			struct Subset // LOD
			{
				uint32_t indexOffset = 0;
				uint32_t indexCount = 0;
			};
			std::vector<Subset> subsets_;	// will be updated via GeometryComponent::update()

			// --- User Custom Buffers ---
			std::vector<std::vector<uint8_t>> customBuffers_;

			PrimitiveType ptype_ = PrimitiveType::TRIANGLES;

			// Non-serialized Attributes:
			geometrics::AABB aabb_;
			XMFLOAT2 uvRangeMin_ = XMFLOAT2(0, 0);
			XMFLOAT2 uvRangeMax_ = XMFLOAT2(1, 1);
			size_t uvStride_ = 0;
			bool useFullPrecisionUV_ = false;
			std::shared_ptr<void> bufferHandle_;	// 'void' refers to GGeometryComponent::GPrimBuffers
			Entity recentBelongingGeometry_ = INVALID_ENTITY;
			bool isConvex = false;

			// BVH
			std::vector<geometrics::AABB> bvhLeafAabbs_;
			geometrics::BVH bvh_;

			// OpenMesh-based data structures for acceleration / editing

			void updateGpuEssentials(); // supposed to be called in GeometryComponent

			// CPU-side BVH acceleration structure
			//  this is supposed to be called by GeometryComponent!
			//	true: BVH will be built immediately if it doesn't exist yet
			//	false: BVH will be deleted immediately if it exists
			void updateBVH(const bool value);

		public:
			mutable bool autoUpdateRenderData = true;
			uint32_t shLevel = 0;

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
			inline bool HasRenderData() const { return bufferHandle_ != nullptr; }
			inline bool IsValid() const { return vertexPositions_.size() > 0 && aabb_.IsValid(); }
			inline void SetAABB(const geometrics::AABB& aabb) { aabb_ = aabb; }
			inline void SetPrimitiveType(const PrimitiveType ptype) { ptype_ = ptype; }
			inline bool HasValidBVH() const { return bvh_.IsValid(); }
			inline const geometrics::BVH& GetBVH() const { return bvh_; }
			inline const std::vector<geometrics::AABB>& GetBVHLeafAABBs() const { return bvhLeafAabbs_; }
			inline bool IsConvexShape() const { return isConvex; }
			inline size_t GetMemoryUsageCPU() const;

			// ----- Getters -----
			inline const std::vector<XMFLOAT3>& GetVtxPositions() const { return vertexPositions_; }
			inline const std::vector<uint32_t>& GetIdxPrimives() const { return indexPrimitives_; }
			inline const std::vector<XMFLOAT3>& GetVtxNormals() const { return vertexNormals_; }
			inline const std::vector<XMFLOAT4>& GetVtxTangents() const { return vertexTangents_; }
			inline const std::vector<XMFLOAT2>& GetVtxUVSet0() const { return vertexUVset0_; }
			inline const std::vector<XMFLOAT2>& GetVtxUVSet1() const { return vertexUVset1_; }
			inline const std::vector<uint32_t>& GetVtxColors() const { return vertexColors_; }
			inline const std::vector<std::vector<uint8_t>>& GetCustomBuffers() const { return customBuffers_; }
			inline std::vector<XMFLOAT3>& GetMutableVtxPositions() { return vertexPositions_; }
			inline std::vector<uint32_t>& GetMutableIdxPrimives() { return indexPrimitives_; }
			inline std::vector<XMFLOAT3>& GetMutableVtxNormals() { return vertexNormals_; }
			inline std::vector<XMFLOAT4>& GetMutableVtxTangents() { return vertexTangents_; }
			inline std::vector<XMFLOAT2>& GetMutableVtxUVSet0() { return vertexUVset0_; }
			inline std::vector<XMFLOAT2>& GetMutableVtxUVSet1() { return vertexUVset1_; }
			inline std::vector<uint32_t>& GetMutableVtxColors() { return vertexColors_; }
			inline std::vector<std::vector<uint8_t>>& GetMutableCustomBuffers() { return customBuffers_; }	

			inline std::vector<Subset>& GetSubsets() { return subsets_; }

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
			void FillIndicesFromTriVertices();
			void ComputeNormals(NormalComputeMethod computeMode);
			void ComputeAABB();
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
		uint32_t partLODs_ = 0; // note: each prim of parts_ has the same LOD

		// Non-serialized attributes
		bool isDirty_ = true;	// BVH, AABB, ...
		bool hasRenderData_ = false;
		bool hasBVH_ = false;
		geometrics::AABB aabb_; // not serialized (automatically updated)
		std::shared_ptr<WaitForBool> waiter_ = std::make_shared<WaitForBool>();

		TimeStamp timeStampPrimitiveUpdate_ = TimerMin;
		TimeStamp timeStampBVHUpdate_ = TimerMin;

		void update();
	public:
		GeometryComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::GEOMETRY, entity, vuid) {}
		virtual ~GeometryComponent() = default;

		bool IsDirtyBVH() const { return TimeDurationCount(timeStampPrimitiveUpdate_, timeStampBVHUpdate_) >= 0; }
		bool HasBVH() const { return hasBVH_; }
		bool IsDirty() { return isDirty_; }
		const geometrics::AABB& GetAABB() { return aabb_; }

		uint32_t GetLODCount() const { return partLODs_; }

		// ----- WaitForBool -----
		void MovePrimitivesFrom(std::vector<Primitive>&& primitives);
		void CopyPrimitivesFrom(const std::vector<Primitive>& primitives);
		void MovePrimitiveFrom(Primitive&& primitive, const size_t slot);
		void CopyPrimitiveFrom(const Primitive& primitive, const size_t slot);
		void AddMovePrimitiveFrom(Primitive&& primitive);
		void AddCopyPrimitiveFrom(const Primitive& primitive);
		void ClearGeometry();
		Primitive* GetMutablePrimitive(const size_t slot);
		// ----- ----------- -----
		
		const Primitive* GetPrimitive(const size_t slot) const;
		const std::vector<Primitive>& GetPrimitives() const { return parts_; }
		size_t GetNumParts() const { return parts_.size(); }
		void SetTessellationFactor(const float tessllationFactor) { tessellationFactor_ = tessllationFactor; }
		float GetTessellationFactor() const { return tessellationFactor_; }

		void UpdateBVH(const bool value);
		bool IsBusyForBVH() { return !waiter_->isFree(); }

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		// GPU interfaces //
		bool HasRenderData() const { return hasRenderData_; }
		bool IsGPUBVHEnabled() const { return isGPUBVHEnabled_; }
		void SetGPUBVHEnabled(const bool value) { isGPUBVHEnabled_ = value; }

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
		bool hasRenderData_ = false;

	public:
		TextureComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::TEXTURE, entity, vuid) {};
		TextureComponent(const ComponentType ctype, const Entity entity, const VUID vuid = 0) : ComponentBase(ctype, entity, vuid) {}
		virtual ~TextureComponent() = default;

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
		inline TextureFormat GetFormat() const { return textureFormat_; }

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

		friend TextureComponent;
	public:
		VolumeComponent(const Entity entity, const VUID vuid = 0) : TextureComponent(ComponentType::VOLUMETEXTURE, entity, vuid) {}
		virtual ~VolumeComponent() = default;

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

	// physics
	struct CORE_EXPORT ColliderComponent : ComponentBase
	{
		enum FLAGS : uint32_t
		{
			EMPTY = 0,
			CPU = 1 << 0,
			GPU = 1 << 1,
			CAPSULE_SHADOW = 1 << 2,
		};

		enum class Shape : uint8_t
		{
			Sphere,
			Capsule,
			Plane,
		};

	protected:
		uint32_t flags_ = CPU;
		Shape shape_ = Shape::Sphere;

		float radius_ = 0;
		XMFLOAT3 offset_ = {};
		XMFLOAT3 tail_ = {};

		// Non-serialized attributes:

	public:
		ColliderComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::COLLIDER, entity, vuid) {}
		virtual ~ColliderComponent() = default;

		// Non-serialized attributes:
		uint32_t indexCpuEnabled = 0;
		uint32_t indexGpuEnabled = 0;
		geometrics::Sphere sphere;
		geometrics::Capsule capsule;
		geometrics::Plane plane;
		uint32_t layerMask = ~0u;
		float dist = 0.f;

		inline void SetCPUEnabled(bool value = true) { if (value) { flags_ |= CPU; } else { flags_ &= ~CPU; } timeStampSetter_ = TimerNow; }
		inline void SetGPUEnabled(bool value = true) { if (value) { flags_ |= GPU; } else { flags_ &= ~GPU; } timeStampSetter_ = TimerNow; }
		inline void SetCapsuleShadowEnabled(bool value = true) { if (value) { flags_ |= CAPSULE_SHADOW; } else { flags_ &= ~CAPSULE_SHADOW; } timeStampSetter_ = TimerNow; }

		inline void SetShape(const Shape value) { shape_ = value; timeStampSetter_ = TimerNow; }
		inline void SetRadius(const float value) { radius_ = value; timeStampSetter_ = TimerNow; }
		inline void SetOffset(const XMFLOAT3 value) { offset_ = value; timeStampSetter_ = TimerNow; }
		inline void SetTail(const XMFLOAT3 value) { tail_ = value; timeStampSetter_ = TimerNow; }
		inline Shape GetShape() const { return shape_; }
		inline float GetRadius() const { return radius_; }
		inline XMFLOAT3 GetOffset() const { return offset_; }
		inline XMFLOAT3 GetTail() const { return tail_; }

		inline bool IsCPUEnabled() const { return flags_ & CPU; }
		inline bool IsGPUEnabled() const { return flags_ & GPU; }
		inline bool IsCapsuleShadowEnabled() const { return flags_ & CAPSULE_SHADOW; }

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::COLLIDER;
	};

	// scene 
	struct CORE_EXPORT RenderableComponent : ComponentBase
	{
		enum RenderableFlags : uint32_t
		{
			EMPTY = 0,
			UNPICKABLE = 1 << 0,
			REQUEST_PLANAR_REFLECTION = 1 << 1,
			LIGHTMAP_RENDER_REQUEST = 1 << 2,
			LIGHTMAP_DISABLE_BLOCK_COMPRESSION = 1 << 3,
			FOREGROUND = 1 << 4,
			CLIP_BOX = 1 << 5,
			CLIP_PLANE = 1 << 6,
			JITTER_SAMPLE = 1 << 7,
			SLICER_NO_SOLID_FILL = 1 << 8, // in the case that the geometry is NOT water-tight
			OUTLINE = 1 << 9,
			UNDERCUT = 1 << 10,
			DISABLE_SHADOW_CAST = 1 << 11,
			DISABLE_SHADOW_RECEIVE = 1 << 12,
			INVISIBLE_IN_MAIN_CAMERA = 1 << 13,
			INVISIBLE_IN_REFLECTIONS = 1 << 14,
			WETMAP_ENABLED = 1 << 15,
		};
		enum RenderableType : uint8_t
		{
			UNDEFINED = 0,
			MESH_RENDERABLE,
			VOLUME_RENDERABLE,
			GSPLAT_RENDERABLE,
			SPRITE_RENDERABLE,
			SPRITEFONT_RENDERABLE,
			ALLTYPES_RENDERABLE,
		};
	private:
		uint32_t flags_ = RenderableFlags::EMPTY;
		RenderableType renderableType_ = RenderableType::UNDEFINED;
		RenderableType renderableReservedType_ = RenderableType::ALLTYPES_RENDERABLE;

		StencilRef engineStencilRef_ = StencilRef::STENCILREF_DEFAULT;
		uint8_t userStencilRef_ = 0;

		VUID vuidGeometry_ = INVALID_ENTITY;
		std::vector<VUID> vuidMaterials_;

		// parameters for visibility effect
		XMFLOAT3 visibleCenter_ = XMFLOAT3(0, 0, 0);
		float visibleRadius_ = 0;
		float fadeDistance_ = std::numeric_limits<float>::max(); // object will begin to fade out at this distance to camera
		uint32_t cascadeMask_ = 0; // which shadow cascades to skip from lowest detail to highest detail (0: skip none, 1: skip first, etc...)
		XMFLOAT4 rimHighlightColor_ = XMFLOAT4(1, 1, 1, 0);
		float rimHighlightFalloff_ = 8;
		float lodBias_ = 0;
		float alphaRef_ = 1;
		float lineThickness_ = 1.3f;	// used for line thickness for rendering

		// clipper
		XMFLOAT4X4 clipBox_ = math::IDENTITY_MATRIX; // WS to origin-centered unit cube
		XMFLOAT4 clipPlane_ = XMFLOAT4(0, 0, 1, 1);

		// special effects
		float outlineThickness_ = 0.f; // zero means 1 pixel
		XMFLOAT3 outlineColor_ = XMFLOAT3(1, 1, 1);
		float outlineThreshold_ = 1000.f;
		XMFLOAT3 undercutDirection_ = XMFLOAT3(0, -1, 0);
		XMFLOAT3 undercutColor_ = XMFLOAT3(1, 0, 0);

		// Non-serialized attributes:
		//	dirty check can be considered by the following components
		//		- transformComponent, geometryComponent, and material components (with their referencing textureComponents)
		bool isDirty_ = true;
		geometrics::AABB aabb_; // world AABB

		void updateRenderableFlags();
	public:
		RenderableComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::RENDERABLE, entity, vuid) {}
		virtual ~RenderableComponent() = default;

		// Non-serialized attributes:
		// caches for world space convention, updated during RenderableComponent::Update()
		XMFLOAT3 center = XMFLOAT3(0, 0, 0);
		float radius = 0;

		inline void SetGeometry(const Entity geometryEntity);
		inline void SetMaterial(const Entity materialEntity, const size_t slot);
		inline void SetMaterials(const std::vector<Entity>& materials);

		inline Entity GetGeometry() const;
		inline Entity GetMaterial(const size_t slot) const;
		inline std::vector<Entity> GetMaterials() const;
		inline size_t GetNumParts() const;
		inline size_t GetMaterials(Entity* entities) const;
		inline const geometrics::AABB& GetAABB() const { return aabb_; }

		inline void SetDirty() { isDirty_ = true; }
		inline bool IsDirty() const { return isDirty_; }
		inline bool IsRenderable() const { return renderableType_ != RenderableType::UNDEFINED; }
		inline RenderableType GetRenderableType() const { return renderableType_; }
		inline void ReserveRenderableType(const RenderableType rType) { renderableReservedType_ = rType; timeStampSetter_ = TimerNow; }

		inline void SetForeground(const bool value) { FLAG_SETTER(flags_, RenderableFlags::FOREGROUND) timeStampSetter_ = TimerNow; }
		inline bool IsForeground() const { return flags_ & RenderableFlags::FOREGROUND; }
		inline void SetPickable(const bool value) { UNFLAG_SETTER(flags_, RenderableFlags::UNPICKABLE) timeStampSetter_ = TimerNow; }
		inline bool IsPickable() const { return !(flags_ & RenderableFlags::UNPICKABLE); }

		inline uint32_t GetFlags() const { return flags_; }

		inline void SetFadeDistance(const float fadeDistance) { fadeDistance_ = fadeDistance; timeStampSetter_ = TimerNow; }
		inline void SetVisibleRadius(const float radius) { visibleRadius_ = radius; timeStampSetter_ = TimerNow; }
		inline void SetVisibleCenter(const XMFLOAT3 center) { visibleCenter_ = center; timeStampSetter_ = TimerNow; }
		inline void SetCascadeMask(const uint32_t cascadeMask) { cascadeMask_ = cascadeMask; timeStampSetter_ = TimerNow; }
		inline void SetLODBias(const float lodBias) { lodBias_ = lodBias; timeStampSetter_ = TimerNow; }

		inline void SetClipperEnabled(const bool clipBoxEnabled, const bool clipPlaneEnabled) {
			clipBoxEnabled ? flags_ |= RenderableFlags::CLIP_BOX : flags_ &= ~RenderableFlags::CLIP_BOX;
			clipPlaneEnabled ? flags_ |= RenderableFlags::CLIP_PLANE : flags_ &= ~RenderableFlags::CLIP_PLANE; timeStampSetter_ = TimerNow;
		}
		inline void SetClipPlane(const XMFLOAT4& clipPlane) { clipPlane_ = clipPlane; timeStampSetter_ = TimerNow; }
		inline void SetClipBox(const XMFLOAT4X4& clipBox) { clipBox_ = clipBox; timeStampSetter_ = TimerNow; }
		inline bool IsBoxClipperEnabled() const { return flags_ & RenderableFlags::CLIP_BOX; }
		inline bool IsPlaneClipperEnabled() const { return flags_ & RenderableFlags::CLIP_PLANE; };

		inline void SetAlphaRef(const float alphaRef) { alphaRef_ = alphaRef; timeStampSetter_ = TimerNow; }
		inline float GetAlphaRef() const { return alphaRef_; }

		inline void SetOutineThickness(const float v) { outlineThickness_ = v; timeStampSetter_ = TimerNow; }
		inline void SetOutineColor(const XMFLOAT3& v) { outlineColor_ = v; timeStampSetter_ = TimerNow; }
		inline void SetOutineThreshold(const float v) { outlineThreshold_ = v; timeStampSetter_ = TimerNow; }
		inline void SetUndercutDirection(const XMFLOAT3& v) { undercutDirection_ = v; timeStampSetter_ = TimerNow; }
		inline void SetUndercutColor(const XMFLOAT3& v) { undercutColor_ = v; timeStampSetter_ = TimerNow; }
		inline void SetOutlineEnabled(const bool value) { FLAG_SETTER(flags_, RenderableFlags::OUTLINE); timeStampSetter_ = TimerNow; }
		inline void SetUndercutEnabled(const bool value) { FLAG_SETTER(flags_, RenderableFlags::UNDERCUT); timeStampSetter_ = TimerNow; }

		inline void SetInvisibleInMainCamera(bool value) { FLAG_SETTER(flags_, RenderableFlags::INVISIBLE_IN_MAIN_CAMERA); timeStampSetter_ = TimerNow; }
		// With this you can disable object rendering for reflections
		inline void SetInvisibleInReflections(bool value) { FLAG_SETTER(flags_, RenderableFlags::INVISIBLE_IN_REFLECTIONS); timeStampSetter_ = TimerNow; }
		inline void SetWetmapEnabled(bool value) { FLAG_SETTER(flags_, RenderableFlags::WETMAP_ENABLED); timeStampSetter_ = TimerNow; }

		inline void SetSlicerSolidFillEnabled(const bool value) { UNFLAG_SETTER(flags_, RenderableFlags::SLICER_NO_SOLID_FILL); timeStampSetter_ = TimerNow; }

		inline void DisableShadowCast(const bool value) { FLAG_SETTER(flags_, RenderableFlags::DISABLE_SHADOW_CAST); timeStampSetter_ = TimerNow; }
		inline void DisableShadowReceive(const bool value) { FLAG_SETTER(flags_, RenderableFlags::DISABLE_SHADOW_RECEIVE); timeStampSetter_ = TimerNow; }

		inline bool IsShadowCastDisabled() const { return flags_ & SCU32(RenderableFlags::DISABLE_SHADOW_CAST); }
		inline bool IsShadowReceiveDisabled() const { return flags_ & SCU32(RenderableFlags::DISABLE_SHADOW_RECEIVE); }
		inline bool IsSlicerSolidFill() const { return !(flags_ & RenderableFlags::SLICER_NO_SOLID_FILL); };

		inline bool IsInvisibleInMainCamera() const { return flags_ & INVISIBLE_IN_MAIN_CAMERA; }
		inline bool IsInvisibleInReflections() const { return flags_ & INVISIBLE_IN_REFLECTIONS; }
		inline bool IsWetmapEnabled() const { return flags_ & WETMAP_ENABLED; }

		inline float GetFadeDistance() const { return fadeDistance_; }
		inline float GetVisibleRadius() const { return visibleRadius_; }
		inline XMFLOAT3 GetVisibleCenter() const { return visibleCenter_; }
		inline uint32_t GetCascadeMask() const { return cascadeMask_; }		
		inline float GetLODBias() const { return lodBias_; }
		inline XMFLOAT4 GetRimHighLightColor() const { return rimHighlightColor_; }
		inline float GetRimHighLightFalloff() const { return rimHighlightFalloff_; }
		inline XMFLOAT4 GetClipPlane() const { return clipPlane_; }
		inline XMFLOAT4X4 GetClipBox() const { return clipBox_; }

		inline float GetOutlineThickness() const { return outlineThickness_; }
		inline XMFLOAT3 GetOutlineColor() const { return outlineColor_; }
		inline float GetOutlineThreshold() const { return outlineThreshold_; }
		inline XMFLOAT3 GetUndercutDirection() const { return undercutDirection_; }
		inline XMFLOAT3 GetUndercutColor() const { return undercutColor_; }

		inline void SetStencilRef(const StencilRef value) { engineStencilRef_ = value; timeStampSetter_ = TimerNow; }
		inline StencilRef GetStencilRef() const { return engineStencilRef_; }
		// User stencil value can be in range [0, 15]
		inline void SetUserStencilRef(const uint8_t value) { userStencilRef_ = value & 0x0F; timeStampSetter_ = TimerNow; }
		inline uint32_t GetUserStencilRef() const { return CombineStencilrefs(engineStencilRef_, userStencilRef_); }

		inline void SetLineThickness(const float lineThickness) { lineThickness_ = lineThickness; timeStampSetter_ = TimerNow; }
		inline float GetLineThickness() { return lineThickness_; }

		inline void Update();

		void ResetRefComponents(const VUID vuidRef) override;
		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::RENDERABLE;
	};

#define ENABLE_FLAG(V, FLAG) { V ? flags_ |= FLAG : flags_ &= ~FLAG; } timeStampSetter_ = TimerNow;

	struct CORE_EXPORT SpriteComponent : ComponentBase
	{
	public:
		enum FLAGS : uint32_t
		{
			EMPTY = 0,
			HIDDEN = 1 << 0,
			DISABLE_UPDATE = 1 << 1,
			CAMERA_FACING = 1 << 2,
			CAMERA_SCALING = 1 << 3,
			EXTRACT_NORMALMAP = 1 << 4,
			MIRROR = 1 << 5,
			OUTPUT_COLOR_SPACE_HDR10_ST2084 = 1 << 6,
			OUTPUT_COLOR_SPACE_LINEAR = 1 << 7,
			CORNER_ROUNDING = 1 << 8,
			DEPTH_TEST = 1 << 9,
			HIGHLIGHT = 1 << 10,
		};

		struct Anim
		{
			struct MovingTexAnim
			{
				float speedX = 0; // the speed of texture scrolling animation in horizontal direction
				float speedY = 0; // the speed of texture scrolling animation in vertical direction
			};
			struct DrawRectAnim
			{
				float frameRate = 30; // target frame rate of the spritesheet animation (eg. 30, 60, etc.)
				int frameCount = 1; // how many frames are in the animation in total
				int horizontalFrameCount = 0; // how many horizontal frames there are (optional, use if the spritesheet contains multiple rows)

				float _elapsedTime = 0; // internal use; you don't need to initialize
				int _currentFrame = 0; // internal use; you don't need to initialize

				void restart()
				{
					_elapsedTime = 0;
					_currentFrame = 0;
				}
			};
			struct WobbleAnim
			{
				XMFLOAT2 amount = XMFLOAT2(0, 0);	// how much the sprite wobbles in X and Y direction
				float speed = 1; // how fast the sprite wobbles

				float corner_angles[4]; // internal use; you don't need to initialize
				float corner_speeds[4]; // internal use; you don't need to initialize
				float corner_angles2[4]; // internal use; you don't need to initialize
				float corner_speeds2[4]; // internal use; you don't need to initialize
				WobbleAnim()
				{
					for (int i = 0; i < 4; ++i)
					{
						corner_angles[i] = random::GetRandom(0, 1000) / 1000.0f * XM_2PI;
						corner_speeds[i] = random::GetRandom(500, 1000) / 1000.0f;
						if (random::GetRandom(0, 1) == 0)
						{
							corner_speeds[i] *= -1;
						}
						corner_angles2[i] = random::GetRandom(0, 1000) / 1000.0f * XM_2PI;
						corner_speeds2[i] = random::GetRandom(500, 1000) / 1000.0f;
						if (random::GetRandom(0, 1) == 0)
						{
							corner_speeds2[i] *= -1;
						}
					}
				}
			};

			bool repeatable = false;
			XMFLOAT3 vel = XMFLOAT3(0, 0, 0);
			float rot = 0;
			float scaleX = 0;
			float scaleY = 0;
			float opa = 0;
			float fad = 0;
			MovingTexAnim movingTexAnim;
			DrawRectAnim drawRectAnim;
			WobbleAnim wobbleAnim;
		};
	protected:
		uint32_t flags_ = DEPTH_TEST;

		VUID vuidSpriteTexture_ = INVALID_VUID;
		Anim anim_;

		// geometry attributes
		XMFLOAT3 position_ = XMFLOAT3(0, 0, 0); //3D
		float rotation_ = 0.f; // angle (rad)
		XMFLOAT2 scale_ = XMFLOAT2(1, 1); // 2D
		float opacity_ = 1.f;
		float fade_ = 0.f;
		XMFLOAT2 uvOffset_ = XMFLOAT2(0, 0);

		// Non-serialized attributes:
		XMFLOAT4 drawRect_ = XMFLOAT4(0, 0, 0, 0);
		XMFLOAT2 corners_[4] = {
			XMFLOAT2(0, 0), XMFLOAT2(1, 0),
			XMFLOAT2(0, 1), XMFLOAT2(1, 1)
			};

	public:
		SpriteComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::SPRITE, entity, vuid) {}
		virtual ~SpriteComponent() = default;

		inline void SetSpriteTexture(const Entity entity);
		inline VUID GetSpriteTextureVUID() const { return vuidSpriteTexture_; }

		inline void SetHidden(bool enabled = true) { ENABLE_FLAG(enabled, HIDDEN); }
		inline void SetUpdateEnabled(bool enabled = true) { ENABLE_FLAG(!enabled, DISABLE_UPDATE); }
		inline void SetCameraFacingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, CAMERA_FACING); }
		inline void SetCameraScalingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, CAMERA_SCALING); }

		inline void SetExtractNormalMapEnabled(bool enabled = true) { ENABLE_FLAG(enabled, EXTRACT_NORMALMAP); }
		inline void SetMirrorEnabled(bool enabled = true) { ENABLE_FLAG(enabled, MIRROR); }
		inline void SetHDR10OutputMappingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, OUTPUT_COLOR_SPACE_HDR10_ST2084); }
		inline void SetLinearOutputMappingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, OUTPUT_COLOR_SPACE_LINEAR); }
		inline void SetCornerRoundingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, CORNER_ROUNDING); }
		inline void SetDepthTestEnabled(bool enabled = true) { ENABLE_FLAG(enabled, DEPTH_TEST); }
		inline void SetHighlightEnabled(bool enabled = true) { ENABLE_FLAG(enabled, HIGHLIGHT); }

		inline bool IsDisableUpdate() const { return flags_ & DISABLE_UPDATE; }
		inline bool IsCameraFacing() const { return flags_ & CAMERA_FACING; }
		inline bool IsCameraScaling() const { return flags_ & CAMERA_SCALING; }
		inline bool IsHidden() const { return flags_ & HIDDEN; }
		inline bool IsExtractNormalMapEnabled() const { return flags_ & EXTRACT_NORMALMAP; }
		inline bool IsMirrorEnabled() const { return flags_ & MIRROR; }
		inline bool IsHDR10OutputMappingEnabled() const { return flags_ & OUTPUT_COLOR_SPACE_HDR10_ST2084; }
		inline bool IsLinearOutputMappingEnabled() const { return flags_ & OUTPUT_COLOR_SPACE_LINEAR; }
		inline bool IsCornerRoundingEnabled() const { return flags_ & CORNER_ROUNDING; }
		inline bool IsDepthTestEnabled() const { return flags_ & DEPTH_TEST; }
		inline bool IsHighlightEnabled() const { return flags_ & HIGHLIGHT; }

		inline void SetPosition(const XMFLOAT3& p) { position_ = p; timeStampSetter_ = TimerNow; }
		inline void SetScale(const XMFLOAT2& s) { scale_ = s; timeStampSetter_ = TimerNow; }
		inline void SetUVOffset(const XMFLOAT2& uvOffset) { uvOffset_ = uvOffset; timeStampSetter_ = TimerNow; }
		inline void SetRotation(const float v) { rotation_ = v; timeStampSetter_ = TimerNow; }
		inline void SetOpacity(const float v) { opacity_ = v; timeStampSetter_ = TimerNow; }
		inline void SetFade(const float v) { fade_ = v; timeStampSetter_ = TimerNow; }

		inline const XMFLOAT3& GetPosition() const { return position_; }
		inline const XMFLOAT2& GetScale() const { return scale_; }
		inline const XMFLOAT2& GetUVOffset() const { return uvOffset_; }
		inline float GetRotation() const { return rotation_; }
		inline float GetOpacity() const { return opacity_; }
		inline float GetFade() const { return fade_; }

		void FixedUpdate();
		void Update(float dt);

		void Serialize(vz::Archive& archive, const uint64_t version) override;
		inline static const ComponentType IntrinsicType = ComponentType::SPRITE;
	};

	struct CORE_EXPORT SpriteFontComponent : ComponentBase
	{
	public:
		enum FLAGS : uint32_t
		{
			EMPTY = 0,
			HIDDEN = 1 << 0,
			DISABLE_UPDATE = 1 << 1,
			CAMERA_FACING = 1 << 2,
			CAMERA_SCALING = 1 << 3,
			SDF_RENDERING = 1 << 4,
			OUTPUT_COLOR_SPACE_HDR10_ST2084 = 1 << 5,
			OUTPUT_COLOR_SPACE_LINEAR = 1 << 6,
			DEPTH_TEST = 1 << 7,
			FLIP_HORIZONTAL = 1 << 8,
			FLIP_VERTICAL = 1 << 9,
		};
		enum Alignment : uint32_t
		{
			FONTALIGN_LEFT,		// left alignment (horizontal)
			FONTALIGN_CENTER,	// center alignment (horizontal or vertical)
			FONTALIGN_RIGHT,	// right alignment (horizontal)
			FONTALIGN_TOP,		// top alignment (vertical)
			FONTALIGN_BOTTOM	// bottom alignment (vertical)
		};
		struct Cursor
		{
			XMFLOAT2 position = {}; // the next character's position offset from the first character (logical canvas units)
			XMFLOAT2 size = {}; // the written text's measurements from the first character (logical canvas units)
		};
		struct Animation
		{
			struct Typewriter
			{
				float time = 0; // time to fully type the text in seconds (0: disable)
				bool looped = false; // if true, typing starts over when finished
				size_t characterStart = 0; // starting character for the animation

				float elapsed = 0; // internal use; you don't need to initialize

				void reset()
				{
					elapsed = 0;
				}
				void Finish()
				{
					elapsed = time;
				}
				bool IsFinished() const
				{
					return time <= elapsed;
				}
			} typewriter;
		};
		inline static int FONTSIZE_DEFAULT = 16;

	protected:

		uint32_t flags_ = DEPTH_TEST;

		std::wstring text_;
		std::string fontStyle_;

		XMFLOAT3 position_ = XMFLOAT3(0, 0, 0); //3D
		int size_ = FONTSIZE_DEFAULT; // line height (logical canvas units)
		float scale_ = 1; // this will apply upscaling to the text while keeping the same resolution (size) of the font
		float rotation_ = 0; // rotation around alignment anchor (in radians)
		XMFLOAT2 spacing_ = XMFLOAT2(0, 0); // minimum spacing between characters (logical canvas units)
		Alignment horizonAlign_ = FONTALIGN_LEFT; // horizontal alignment
		Alignment verticalAlign_ = FONTALIGN_TOP; // vertical alignment
		XMFLOAT4 color_; // base color of the text characters
		XMFLOAT4 shadowColor_; // transparent disables, any other color enables shadow under text
		float wrap_ = -1; // wrap start width (-1 default for no wrap) (logical canvas units)
		float softness_ = 0; // value in [0,1] range (requires SDF rendering to be enabled)
		float bolden_ = 0; // value in [0,1] range (requires SDF rendering to be enabled)
		float shadowSoftness_ = 0.5f; // value in [0,1] range (requires SDF rendering to be enabled)
		float shadowBolden_ = 0.1f; // value in [0,1] range (requires SDF rendering to be enabled)
		XMFLOAT2 shadowOffset_ = XMFLOAT2(0, 0); // offset for shadow under the text in logical canvas coordinates
		float hdrScaling_ = 1.0f; // a scaling value for use by linear output mapping
		float intensity_ = 1.0f; // color multiplier
		float shadowIntensity_ = 1.0f; // shadow color multiplier
		Cursor cursor_; // cursor can be used to continue text drawing by taking the Draw's return value (optional)

		Animation anim_;

	public:
		SpriteFontComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::SPRITEFONT, entity, vuid) {}
		virtual ~SpriteFontComponent() = default;

		inline void SetHiddenEnabled(bool enabled = true) { ENABLE_FLAG(enabled, HIDDEN); }
		inline void SetUpdateEnabled(bool enabled = true) { ENABLE_FLAG(!enabled, DISABLE_UPDATE); }
		inline void SetCameraFacingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, CAMERA_FACING); }
		inline void SetCameraScalingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, CAMERA_SCALING); }

		inline void SetSDFRenderingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, SDF_RENDERING); }
		inline void SetHDR10OutputMappingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, OUTPUT_COLOR_SPACE_HDR10_ST2084); }
		inline void SetLinearOutputMappingEnabled(bool enabled = true) { ENABLE_FLAG(enabled, OUTPUT_COLOR_SPACE_LINEAR); }
		inline void SetDepthTestEnabled(bool enabled = true) { ENABLE_FLAG(enabled, DEPTH_TEST); }
		inline void SetFlipHorizontallyEnabled(bool enabled = true) { ENABLE_FLAG(enabled, FLIP_HORIZONTAL); }
		inline void SetFlipVerticallyEnabled(bool enabled = true) { ENABLE_FLAG(enabled, FLIP_VERTICAL); }

		inline bool IsHidden() const { return flags_ & HIDDEN; }
		inline bool IsDisableUpdate() const { return flags_ & DISABLE_UPDATE; }
		inline bool IsCameraFacing() const { return flags_ & CAMERA_FACING; }
		inline bool IsCameraScaling() const { return flags_ & CAMERA_SCALING; }
		inline bool IsSDFRendering() const { return flags_ & SDF_RENDERING; }
		inline bool IsHDR10OutputMapping() const { return flags_ & OUTPUT_COLOR_SPACE_HDR10_ST2084; }
		inline bool IsLinearOutputMapping() const { return flags_ & OUTPUT_COLOR_SPACE_LINEAR; }
		inline bool IsDepthTest() const { return flags_ & DEPTH_TEST; }
		inline bool IsFlipHorizontally() const { return flags_ & FLIP_HORIZONTAL; }
		inline bool IsFlipVertically() const { return flags_ & FLIP_VERTICAL; }

		void FixedUpdate();
		void Update(float dt);

		void SetText(const std::string& value);

		inline void SetFontStyle(const std::string& fontStyle) { fontStyle_ = fontStyle; timeStampSetter_ = TimerNow; }
		inline void SetPosition(const XMFLOAT3& p) { position_ = p; timeStampSetter_ = TimerNow; }
		inline void SetSize(const int size) { size_ = size; timeStampSetter_ = TimerNow; }
		inline void SetScale(const float scale) { scale_ = scale; timeStampSetter_ = TimerNow; }
		inline void SetRotation(const float rotation) { rotation_ = rotation; timeStampSetter_ = TimerNow; }
		inline void SetSpacing(const XMFLOAT2& spacing) { spacing_ = spacing; timeStampSetter_ = TimerNow; }
		inline void SetHorizonAlign(const Alignment horizonAlign) { horizonAlign_ = horizonAlign; timeStampSetter_ = TimerNow; }
		inline void SetVerticalAlign(const Alignment verticalAlign) { verticalAlign_ = verticalAlign; timeStampSetter_ = TimerNow; }
		inline void SetColor(const XMFLOAT4& color) { color_ = color; timeStampSetter_ = TimerNow; }
		inline void SetShadowColor(const XMFLOAT4& shadowColor) { shadowColor_ = shadowColor; timeStampSetter_ = TimerNow; }
		inline void SetWrap(const float wrap) { wrap_ = wrap; timeStampSetter_ = TimerNow; }
		inline void SetSoftness(const float softness) { softness_ = softness; timeStampSetter_ = TimerNow; }
		inline void SetBolden(const float bolden) { bolden_ = bolden; timeStampSetter_ = TimerNow; }
		inline void SetShadowSoftness(const float shadowSoftness) { shadowSoftness_ = shadowSoftness; timeStampSetter_ = TimerNow; }
		inline void SetShadowBolden(const float shadowBolden) { shadowBolden_ = shadowBolden; timeStampSetter_ = TimerNow; }
		inline void SetShadowOffset(const XMFLOAT2 shadowOffset) { shadowOffset_ = shadowOffset; timeStampSetter_ = TimerNow; }
		inline void SetHdrScale(const float hdrScaling) { hdrScaling_ = hdrScaling; timeStampSetter_ = TimerNow; }
		inline void SetIntensity(const float intensity) { intensity_ = intensity; timeStampSetter_ = TimerNow; }
		inline void SetShadowIntensity(const float shadowIntensity) { shadowIntensity_ = shadowIntensity; timeStampSetter_ = TimerNow; }
		inline void SetCursor(const Cursor& cursor) { cursor_ = cursor; timeStampSetter_ = TimerNow; }

		std::string GetTextA() const;
		const std::wstring& GetText() const { return text_; }
		size_t GetCurrentTextLength() const;

		inline const std::string& GetFontStyle() const { return fontStyle_; }
		inline const XMFLOAT3& GetPosition() const { return position_; }
		inline int GetSize() const { return size_; }
		inline float GetScale() const { return scale_; }
		inline float GetRotation() const { return rotation_; }
		inline const XMFLOAT2& GetSpacing() const { return spacing_; }
		inline const Alignment& GetHorizonAlign() const { return horizonAlign_; }
		inline const Alignment& GetVerticalAlign() const { return verticalAlign_; }
		inline const XMFLOAT4& GetColor() const { return color_; }
		inline const XMFLOAT4& GetShadowColor() const { return shadowColor_; }
		inline float GetWrap() const { return wrap_; }
		inline float GetSoftness() const { return softness_; }
		inline float GetBolden() const { return bolden_; }
		inline float GetShadowSoftness() const { return shadowSoftness_; }
		inline float GetShadowBolden() const { return shadowBolden_; }
		inline const XMFLOAT2& GetShadowOffset() const { return shadowOffset_; }
		inline float GetHdrScale() const { return hdrScaling_; }
		inline float GetIntensity() const { return intensity_; }
		inline float GetShadowIntensity() const { return shadowIntensity_; }
		inline const Cursor& GetCursor() const { return cursor_; }

		void Serialize(vz::Archive& archive, const uint64_t version) override;
		inline static const ComponentType IntrinsicType = ComponentType::SPRITEFONT;
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
			LIGHTMAPONLY_STATIC = 1 << 3,
		};

		uint32_t lightFlag_ = Flags::EMPTY;
		LightType type_ = LightType::DIRECTIONAL;

		XMFLOAT3 color_ = XMFLOAT3(1, 1, 1);
		float range_ = 10.0f;
		// Brightness of light in. The units that this is defined in depend on the type of light. 
		// Point and spot lights use luminous intensity in candela (lm/sr) while directional lights use illuminance in lux (lm/m2). 
		//  refer to: https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_lights_punctual
		float intensity_ = 1.0f; 
		
		float radius_ = 0.025f; // for softshadow sampling or source size of spot/point lights
		float length_ = 0.f;	// for line source (point light-ext) or rectangle light

		// spot light only
		float outerConeAngle_ = XM_PIDIV4;
		float innerConeAngle_ = 0; // default value is 0, means only outer cone angle is used

		// Non-serialized attributes:
		bool isDirty_ = true;

		geometrics::AABB aabb_;

		// note there will be added many attributes to describe the light properties with various lighting techniques
		// refer to filament engine's lightManager and wicked engine's lightComponent
	public:
		LightComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::LIGHT, entity, vuid) {}
		virtual ~LightComponent() = default;

		// Non-serialized attributes:
		// if there is no transformComponent, then use these attributes directly
		// unless, these attributes will be automatically updated during the scene update
		mutable int occlusionquery = -1;

		// Non-serialized attributes: (these variables are supposed to be updated via transformers)
		// read-only
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		XMFLOAT3 direction = XMFLOAT3(0, 0, -1); // forward
		XMFLOAT4 rotation = XMFLOAT4(0, 0, 0, 1);
		XMFLOAT3 scale = XMFLOAT3(1, 1, 1);

		inline bool IsDirty() const { return isDirty_; }

		inline void SetDirty() { isDirty_ = true; timeStampSetter_ = TimerNow; }
		inline void SetColor(XMFLOAT3 color) { color_ = color; timeStampSetter_ = TimerNow; }
		inline void SetRange(const float range) { range_ = range; isDirty_ = true; timeStampSetter_ = TimerNow; }
		inline void SetRadius(const float radius) { radius_ = radius; timeStampSetter_ = TimerNow; }
		inline void SetLength(const float length) { length_ = length; timeStampSetter_ = TimerNow; }
		inline void SetType(LightType type) { type_ = type; isDirty_ = true; timeStampSetter_ = TimerNow; };
		inline void SetIntensity(const float intensity) { intensity_ = intensity; timeStampSetter_ = TimerNow; }
		inline void SetOuterConeAngle(const float angle) { outerConeAngle_ = angle; isDirty_ = true; timeStampSetter_ = TimerNow; }
		inline void SetInnerConeAngle(const float angle) { innerConeAngle_ = angle; isDirty_ = true; timeStampSetter_ = TimerNow; }
		inline void SetCastShadow(bool value) { if (value) { lightFlag_ |= CAST_SHADOW; } else { lightFlag_ &= ~CAST_SHADOW; } isDirty_ = true; timeStampSetter_ = TimerNow; }
		inline void SetVisualizerEnabled(bool value) { if (value) { lightFlag_ |= VISUALIZER; } else { lightFlag_ &= ~VISUALIZER; } isDirty_ = true; timeStampSetter_ = TimerNow; }
		inline void SetStatic(bool value) { if (value) { lightFlag_ |= LIGHTMAPONLY_STATIC; } else { lightFlag_ &= ~LIGHTMAPONLY_STATIC; } isDirty_ = true; timeStampSetter_ = TimerNow; }

		inline XMFLOAT3 GetColor() const { return color_; }
		inline float GetIntensity() const { return intensity_; }
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
		inline LightType GetType() const { return type_; }
		inline float GetOuterConeAngle() const { return outerConeAngle_; }
		inline float GetInnerConeAngle() const { return innerConeAngle_; }

		inline bool IsCastingShadow() const { return lightFlag_ & CAST_SHADOW; }
		inline bool IsVisualizerEnabled() const { return lightFlag_ & VISUALIZER; }
		inline bool IsStatic() const { return lightFlag_ & LIGHTMAPONLY_STATIC; }
		inline bool IsInactive() const { return intensity_ == 0 || range_ == 0; }

		inline void Update();	// if there is a transform entity, make sure the transform is updated!

		inline void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::LIGHT;
	};

	struct CORE_EXPORT CameraComponent : ComponentBase
	{
		enum class DVR_TYPE : uint8_t
		{
			// XRAY_[mode] uses 
			DEFAULT = 0,
			XRAY_AVERAGE,
		};
	protected:
		enum CamFlags : uint32_t
		{
			EMPTY = 0,
			ORTHOGONAL = 1 << 0,    // if not, PERSPECTIVE
			INTRINSICS_PROJECTION = 1 << 2,
			CUSTOM_PROJECTION = 1 << 3,
			CLIP_PLANE = 1 << 4,
			CLIP_BOX = 1 << 5,
			SLICER = 1 << 6, // must be ORTHOGONAL
			CURVED = 1 << 7, // must be SLICER
		};

		float zNearP_ = 0.1f;
		float zFarP_ = 10000.0f;
		float fovY_ = XM_PI / 3.0f;
		float focalLength_ = 1;
		float apertureSize_ = 0;
		float orthoVerticalSize_ = 1.f;
		float fx_ = 1.f, fy_ = 1.f, sc_ = 1.f, cx_ = 0.f, cy_ = 0.f;
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

		uint32_t flags_ = CamFlags::EMPTY;

		DVR_TYPE dvrType_ = DVR_TYPE::DEFAULT;
		MaterialComponent::LookupTableSlot dvrLookup_ = MaterialComponent::LookupTableSlot::LOOKUP_OTF;

		// clipper
		XMFLOAT4X4 clipBox_ = math::IDENTITY_MATRIX; // WS to origin-centered unit cube
		XMFLOAT4 clipPlane_ = XMFLOAT4(0, 0, 1, 1);

		// Non-serialized attributes:
		bool isDirty_ = true;
		XMFLOAT3 eye_ = XMFLOAT3(0, 0, 0);
		XMFLOAT3 at_ = XMFLOAT3(0, 0, 1);
		XMFLOAT3 forward_ = XMFLOAT3(0, 0, -1); // viewing direction
		XMFLOAT3 up_ = XMFLOAT3(0, 1, 0);
		XMFLOAT3X3 rotationMatrix_ = math::IDENTITY_MATRIX33;	// used for the rendering object to face camera
		XMFLOAT4X4 view_ = math::IDENTITY_MATRIX;
		XMFLOAT4X4 projection_ = math::IDENTITY_MATRIX;
		XMFLOAT4X4 projectionJitterFree_ = math::IDENTITY_MATRIX;
		XMFLOAT4X4 viewProjection_ = math::IDENTITY_MATRIX;
		XMFLOAT4X4 invView_ = math::IDENTITY_MATRIX;
		XMFLOAT4X4 invProjection_ = math::IDENTITY_MATRIX;
		XMFLOAT4X4 invViewProjection_ = math::IDENTITY_MATRIX;
		vz::geometrics::Frustum frustum_ = {};

		float computeOrthoVerticalSizeFromPerspective(const float dist);

	public:
		CameraComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::CAMERA, entity, vuid) {}
		CameraComponent(const ComponentType ctype, const Entity entity, const VUID vuid = 0) : ComponentBase(ctype, entity, vuid) {}
		virtual ~CameraComponent() = default;

		// Non-serialized attributes:
		XMFLOAT2 jitter = XMFLOAT2(0, 0);

		inline void SetDirty() { isDirty_ = true; }
		inline bool IsDirty() const { return isDirty_; }
		// consider TransformComponent and HierarchyComponent that belong to this CameraComponent entity
		inline bool SetWorldLookAtFromHierarchyTransforms();
		inline void SetWorldLookTo(const XMFLOAT3& eye, const XMFLOAT3& view, const XMFLOAT3& up) {
			XMFLOAT3 at; XMStoreFloat3(&at, XMLoadFloat3(&eye) + XMLoadFloat3(&view)); SetWorldLookAt(eye, at, up);
		}

		virtual void SetWorldLookAt(const XMFLOAT3& eye, const XMFLOAT3& at, const XMFLOAT3& up);
		virtual void SetPerspective(const float width, const float height, const float nearP, const float farP, const float fovY = XM_PI / 3.0f);
		virtual void SetOrtho(const float width, const float height, const float nearP, const float farP, const float orthoVerticalSize);
		virtual void SetIntrinsicsProjection(const float width, const float height, const float nearP, const float farP, const float fx, const float fy, const float cx, const float cy, const float s = 1.f);

		inline void SetClipperEnabled(const bool clipBoxEnabled, const bool clipPlaneEnabled) {
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
		inline const XMFLOAT3X3& GetRotationToFaceCamera() const { return rotationMatrix_; }
		inline const XMFLOAT4X4& GetView() const { return view_; }
		inline const XMFLOAT4X4& GetProjection() const { return projection_; }
		inline const XMFLOAT4X4& GetProjectionJitterFree() const { return projectionJitterFree_; }
		inline const XMFLOAT4X4& GetViewProjection() const { return viewProjection_; }
		inline const XMFLOAT4X4& GetInvView() const { return invView_; }
		inline const XMFLOAT4X4& GetInvProjection() const { return invProjection_; }
		inline const XMFLOAT4X4& GetInvViewProjection() const { return invViewProjection_; }
		inline const geometrics::Frustum& GetFrustum() const { return frustum_; }

		inline bool IsOrtho() const { return flags_ & ORTHOGONAL; } // if not perspective
		inline bool IsIntrinsicsProjection() const { return flags_ & INTRINSICS_PROJECTION; }
		inline bool IsCustomProjection() const { return flags_ & CUSTOM_PROJECTION; }
		inline bool IsSlicer() const { return flags_ & SLICER; }	// implying OTRHOGONAL
		inline bool IsCurvedSlicer() const { return flags_ & CURVED; }	// implying SLICER
		inline float GetFovVertical() const { return fovY_; }
		inline float GetFocalLength() const { return focalLength_; }
		inline float GetApertureSize() const { return apertureSize_; }
		inline XMFLOAT2 GetApertureShape() const { return apertureShape_; }

		inline void GetWidthHeight(float* w, float* h) const { if (w) *w = width_; if (h) *h = height_; }
		inline void GetNearFar(float* n, float* f) const { if (n) *n = zNearP_; if (f) *f = zFarP_; }
		inline float GetOrthoVerticalSize() const { return orthoVerticalSize_; }
		inline void GetIntrinsics(float* fx, float* fy, float* cx, float* cy, float* sc) {
			if (fx) *fx = fx_; if (fy) *fy = fy_; if (cx) *cx = cx_; if (cy) *cy = cy_; if (sc) *sc = sc_;
		};

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

		inline void SetDVRLookupSlot(const MaterialComponent::LookupTableSlot slot) { dvrLookup_ = slot; timeStampSetter_ = TimerNow; }
		inline MaterialComponent::LookupTableSlot GetDVRLookupSlot() const { return dvrLookup_; }

		inline void SetDVRType(const DVR_TYPE type) { dvrType_ = type; timeStampSetter_ = TimerNow; }
		inline DVR_TYPE GetDVRType() const { return dvrType_; }

		virtual LayeredMaskComponent* GetLayeredMaskComponent() const { return nullptr; }
		virtual TransformComponent* GetTransformComponent() const { return nullptr; }
		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::CAMERA;
	};

	struct CORE_EXPORT SlicerComponent : CameraComponent
	{
	protected:
		float thickness_ = 0.f;
		float outlineThickness_ = 1.f; // in pixel unit

		// Curved-slicer attributes
		std::vector<XMFLOAT3> horizontalCurveControls_;
		float curveInterpolationInterval_ = 0.01f;
		float curvedPlaneHeight_ = 1.f;
		bool isReverseSide_ = false;
		XMFLOAT3 curvedSlicerUp_ = XMFLOAT3(0, 0, 1);

		// Non-serialized attributes:
		float curvedPlaneWidth_ = 1.f;
		bool isDirtyCurve_ = true;
		std::vector<XMFLOAT3> horizontalCurveInterpPoints_;
	public:
		SlicerComponent(const Entity entity, const bool curvedSlicer, const VUID vuid = 0) : CameraComponent(ComponentType::SLICER, entity, vuid) {
			flags_ = CamFlags::ORTHOGONAL | CamFlags::SLICER | (curvedSlicer? CamFlags::CURVED : 0);
			zNearP_ = 0.f;
		}
		virtual ~SlicerComponent() = default;

		inline void SetThickness(const float value) { thickness_ = value; timeStampSetter_ = TimerNow; }
		inline float GetThickness() const { return thickness_; }
		// if value <= 0. then, apply Actor's outlineThickess to the slicer's outline
		inline void SetOutlineThickness(const float value) { outlineThickness_ = value; timeStampSetter_ = TimerNow; }
		inline float GetOutlineThickness() const { return outlineThickness_; }

		void SetWorldLookAt(const XMFLOAT3& eye, const XMFLOAT3& at, const XMFLOAT3& up) override;
		void SetPerspective(const float width, const float height, const float nearP, const float farP, const float fovY = XM_PI / 3.0f) override;
		void SetOrtho(const float width, const float height, const float nearP, const float farP, const float orthoVerticalSize) override;
		void SetIntrinsicsProjection(const float width, const float height, const float nearP, const float farP, const float fx, const float fy, const float cx, const float cy, const float s = 0.f) override;

		// Interfaces for Curved Slicer
		inline void SetHorizontalCurveControls(const std::vector<XMFLOAT3>& controlPts, const float interval) { horizontalCurveControls_ = controlPts; curveInterpolationInterval_ = interval; isDirtyCurve_ = true; timeStampSetter_ = TimerNow; };
		inline const std::vector<XMFLOAT3>& GetHorizontalCurveControls() const { return horizontalCurveControls_; }
		inline const std::vector<XMFLOAT3>& GetHorizontalCurveInterpPoints() const { return horizontalCurveInterpPoints_; }
		inline void SetCurvedPlaneHeight(const float value) { curvedPlaneHeight_ = value; timeStampSetter_ = TimerNow; }
		inline float GetCurvedPlaneWidth() const { return curvedPlaneWidth_; }
		inline float GetCurvedPlaneHeight() const { return curvedPlaneHeight_; }
		inline void SetCurvedPlaneUp(const XMFLOAT3& up) { curvedSlicerUp_ = up; timeStampSetter_ = TimerNow; };
		inline XMFLOAT3 GetCurvedPlaneUp() const { return curvedSlicerUp_;  }
		inline void SetReverseSide(const bool reversed) { isReverseSide_ = reversed; timeStampSetter_ = TimerNow; }
		inline bool IsReverseSide() const { return isReverseSide_; }
		inline bool IsValidCurvedPlane() const { return horizontalCurveControls_.size() > 2 && curvedPlaneHeight_ > 0; }
		inline bool MakeCurvedSlicerHelperGeometry(const Entity geometryEntity);

		virtual void UpdateCurve() = 0;

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::SLICER;
	};

	struct CORE_EXPORT EnvironmentComponent : ComponentBase
	{
		enum FLAGS
		{
			EMPTY = 0,
			OCEAN_ENABLED = 1 << 0,
			REALISTIC_SKY = 1 << 1,
			VOLUMETRIC_CLOUDS = 1 << 2,
			HEIGHT_FOG = 1 << 3,
			VOLUMETRIC_CLOUDS_CAST_SHADOW = 1 << 4,
			OVERRIDE_FOG_COLOR = 1 << 5,
			REALISTIC_SKY_AERIAL_PERSPECTIVE = 1 << 6,
			REALISTIC_SKY_HIGH_QUALITY = 1 << 7,
			REALISTIC_SKY_RECEIVE_SHADOW = 1 << 8,
			VOLUMETRIC_CLOUDS_RECEIVE_SHADOW = 1 << 9,
		};

		struct OceanParameters
		{
			// Must be power of 2.
			int dmapDim = 512;
			// Typical value is 1000 ~ 2000
			float patchLength = 50.0f;

			// Adjust the time interval for simulation.
			float timeScale = 0.3f;
			// Amplitude for transverse wave. Around 1.0
			float waveAmplitude = 1000.0f;
			// Wind direction. Normalization not required.
			XMFLOAT2 windDir = XMFLOAT2(0.8f, 0.6f);
			// Around 100 ~ 1000
			float windSpeed = 600.0f;
			// This value damps out the waves against the wind direction.
			// Smaller value means higher wind dependency.
			float windDependency = 0.07f;
			// The amplitude for longitudinal wave. Must be positive.
			float choppyScale = 1.3f;


			XMFLOAT4 waterColor = XMFLOAT4(0.0f, 2.0f / 255.0f, 6.0f / 255.0f, 0.6f);
			XMFLOAT4 extinctionColor = XMFLOAT4(0, 0.9f, 1, 1);
			float waterHeight = 0.0f;
			uint32_t surfaceDetail = 4;
			float surfaceDisplacementTolerance = 2;
		};
		struct AtmosphereParameters
		{
			// Radius of the planet (center to ground)
			float bottomRadius;
			// Maximum considered atmosphere height (center to atmosphere top)
			float topRadius;

			// Center of the planet
			XMFLOAT3 planetCenter;
			// Rayleigh scattering exponential distribution scale in the atmosphere
			float rayleighDensityExpScale;

			// Rayleigh scattering coefficients
			XMFLOAT3 rayleighScattering;
			// Mie scattering exponential distribution scale in the atmosphere
			float mieDensityExpScale;

			// Mie scattering coefficients
			XMFLOAT3 mieScattering;
			// Mie extinction coefficients
			XMFLOAT3 mieExtinction;

			// Mie absorption coefficients
			XMFLOAT3 mieAbsorption;
			// Mie phase function excentricity
			float miePhaseG;

			// Another medium type in the atmosphere
			float absorptionDensity0LayerWidth;
			float absorptionDensity0ConstantTerm;
			float absorptionDensity0LinearTerm;
			float absorptionDensity1ConstantTerm;

			float absorptionDensity1LinearTerm;

			// This other medium only absorb light, e.g. useful to represent ozone in the earth atmosphere
			XMFLOAT3 absorptionExtinction;

			// The albedo of the ground.
			XMFLOAT3 groundAlbedo;

			// Varying sample count for sky rendering based on the 'distanceSPPMaxInv' with min-max
			XMFLOAT2 rayMarchMinMaxSPP;
			// Describes how the raymarching samples are distributed no the screen
			float distanceSPPMaxInv;
			// Aerial Perspective exposure override
			float aerialPerspectiveScale;

			// Init default values (All units in kilometers)
			void Init(
				float earthBottomRadius = 6360.0f,
				float earthTopRadius = 6460.0f, // 100km atmosphere radius, less edge visible and it contain 99.99% of the atmosphere medium https://en.wikipedia.org/wiki/K%C3%A1rm%C3%A1n_line
				float earthRayleighScaleHeight = 8.0f,
				float earthMieScaleHeight = 1.2f
			)
			{

				// Values shown here are the result of integration over wavelength power spectrum integrated with paricular function.
				// Refer to https://github.com/ebruneton/precomputed_atmospheric_scattering for details.

				// Translation from Bruneton2017 parameterisation.
				rayleighDensityExpScale = -1.0f / earthRayleighScaleHeight;
				mieDensityExpScale = -1.0f / earthMieScaleHeight;
				absorptionDensity0LayerWidth = 25.0f;
				absorptionDensity0ConstantTerm = -2.0f / 3.0f;
				absorptionDensity0LinearTerm = 1.0f / 15.0f;
				absorptionDensity1ConstantTerm = 8.0f / 3.0f;
				absorptionDensity1LinearTerm = -1.0f / 15.0f;

				miePhaseG = 0.8f;
				rayleighScattering = XMFLOAT3(0.005802f, 0.013558f, 0.033100f);
				mieScattering = XMFLOAT3(0.003996f, 0.003996f, 0.003996f);
				mieExtinction = XMFLOAT3(0.004440f, 0.004440f, 0.004440f);
				mieAbsorption.x = mieExtinction.x - mieScattering.x;
				mieAbsorption.y = mieExtinction.y - mieScattering.y;
				mieAbsorption.z = mieExtinction.z - mieScattering.z;

				absorptionExtinction = XMFLOAT3(0.000650f, 0.001881f, 0.000085f);

				groundAlbedo = XMFLOAT3(0.3f, 0.3f, 0.3f); // 0.3 for earths ground albedo, see https://nssdc.gsfc.nasa.gov/planetary/factsheet/earthfact.html
				bottomRadius = earthBottomRadius;
				topRadius = earthTopRadius;
				planetCenter = XMFLOAT3(0.0f, -earthBottomRadius - 0.1f, 0.0f); // Spawn 100m in the air

				rayMarchMinMaxSPP = XMFLOAT2(4.0f, 14.0f);
				distanceSPPMaxInv = 0.01f;
				aerialPerspectiveScale = 1.0f;
			}

			AtmosphereParameters() { Init(); }
		};
		struct VolumetricCloudLayer
		{
			// Lighting
			XMFLOAT3 albedo; // Cloud albedo is normally very close to white
			XMFLOAT3 extinctionCoefficient;

			// Modelling
			float skewAlongWindDirection;
			float totalNoiseScale;
			float curlScale;
			float curlNoiseHeightFraction;

			float curlNoiseModifier;
			float detailScale;
			float detailNoiseHeightFraction;
			float detailNoiseModifier;

			float skewAlongCoverageWindDirection;
			float weatherScale;
			float coverageAmount;
			float coverageMinimum;

			float typeAmount;
			float typeMinimum;
			float rainAmount; // Rain clouds disabled by default.
			float rainMinimum;

			// Cloud types: 4 positions of a black, white, white, black gradient
			XMFLOAT4 gradientSmall;
			XMFLOAT4 gradientMedium;
			XMFLOAT4 gradientLarge;

			// amountTop, offsetTop, amountBot, offsetBot: Control with 'amount' the coverage scale along the current gradient height, and can be adjusted with 'offset'
			XMFLOAT4 anvilDeformationSmall;
			XMFLOAT4 anvilDeformationMedium;
			XMFLOAT4 anvilDeformationLarge;

			// Animation
			float windSpeed;
			float windAngle;
			float windUpAmount;
			float coverageWindSpeed;
			float coverageWindAngle;
		};
		struct VolumetricCloudParameters
		{
			float beerPowder;
			float beerPowderPower;
			float ambientGroundMultiplier; // [0; 1] Amount of ambient light to reach the bottom of clouds
			float phaseG; // [-0.999; 0.999]

			float phaseG2; // [-0.999; 0.999]
			float phaseBlend; // [0; 1]
			float multiScatteringScattering;
			float multiScatteringExtinction;

			float multiScatteringEccentricity;
			float shadowStepLength;
			float horizonBlendAmount;
			float horizonBlendPower;

			float cloudStartHeight;
			float cloudThickness;
			float animationMultiplier;

			VolumetricCloudLayer layerFirst;
			VolumetricCloudLayer layerSecond;

			// Performance
			int maxStepCount; // Maximum number of iterations. Higher gives better images but may be slow.
			float maxMarchingDistance; // Clamping the marching steps to be within a certain distance.
			float inverseDistanceStepCount; // Distance over which the raymarch steps will be evenly distributed.
			float renderDistance; // Maximum distance to march before returning a miss.

			float LODDistance; // After a certain distance, noises will get higher LOD
			float LODMin; // 
			float bigStepMarch; // How long inital rays should be until they hit something. Lower values may give a better image but may be slower.
			float transmittanceThreshold; // Default: 0.005. If the clouds transmittance has reached it's desired opacity, there's no need to keep raymarching for performance.

			float shadowSampleCount;
			float groundContributionSampleCount;

			void Init()
			{
				// Lighting
				beerPowder = 20.0f;
				beerPowderPower = 0.5f;
				ambientGroundMultiplier = 0.75f;
				phaseG = 0.5f; // [-0.999; 0.999]
				phaseG2 = -0.5f; // [-0.999; 0.999]
				phaseBlend = 0.2f; // [0; 1]
				multiScatteringScattering = 1.0f;
				multiScatteringExtinction = 0.1f;
				multiScatteringEccentricity = 0.2f;
				shadowStepLength = 3000.0f;
				horizonBlendAmount = 0.0000125f;
				horizonBlendPower = 2.0f;

				// Modelling
				cloudStartHeight = 1500.0f;
				cloudThickness = 5000.0f;

				// First
				{
					// Lighting
					layerFirst.albedo = XMFLOAT3(0.9f, 0.9f, 0.9f);
					layerFirst.extinctionCoefficient = XMFLOAT3(0.71f * 0.1f, 0.86f * 0.1f, 1.0f * 0.1f);

					// Modelling
					layerFirst.skewAlongWindDirection = 700.0f;
					layerFirst.totalNoiseScale = 0.0006f;
					layerFirst.curlScale = 0.3f;
					layerFirst.curlNoiseHeightFraction = 5.0f;
					layerFirst.curlNoiseModifier = 500.0f;
					layerFirst.detailScale = 4.0f;
					layerFirst.detailNoiseHeightFraction = 10.0f;
					layerFirst.detailNoiseModifier = 0.3f;
					layerFirst.skewAlongCoverageWindDirection = 2500.0f;
					layerFirst.weatherScale = 0.00002f;
					layerFirst.coverageAmount = 1.0f;
					layerFirst.coverageMinimum = 0.0f;
					layerFirst.typeAmount = 1.0f;
					layerFirst.typeMinimum = 0.0f;
					layerFirst.rainAmount = 0.0f; // Rain clouds disabled by default.
					layerFirst.rainMinimum = 0.0f;

					// Cloud types: 4 positions of a black, white, white, black gradient
					layerFirst.gradientSmall = XMFLOAT4(0.01f, 0.1f, 0.11f, 0.2f);
					layerFirst.gradientMedium = XMFLOAT4(0.01f, 0.08f, 0.3f, 0.4f);
					layerFirst.gradientLarge = XMFLOAT4(0.01f, 0.06f, 0.75f, 0.95f);

					// amountTop, offsetTop, amountBot, offsetBot: Control with 'amount' the coverage scale along the current gradient height, and can be adjusted with 'offset'
					layerFirst.anvilDeformationSmall = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
					layerFirst.anvilDeformationMedium = XMFLOAT4(15.0f, 0.1f, 15.0f, 0.1f);
					layerFirst.anvilDeformationLarge = XMFLOAT4(5.0f, 0.25f, 5.0f, 0.15f);

					// Animation
					layerFirst.windSpeed = 15.0f;
					layerFirst.windAngle = 0.75f;
					layerFirst.windUpAmount = 0.5f;
					layerFirst.coverageWindSpeed = 30.0f;
					layerFirst.coverageWindAngle = 0.0f;
				}

				// Second
				{
					// Lighting
					layerSecond.albedo = XMFLOAT3(0.9f, 0.9f, 0.9f);
					layerSecond.extinctionCoefficient = XMFLOAT3(0.71f * 0.01f, 0.86f * 0.01f, 1.0f * 0.01f);

					// Modelling
					layerSecond.skewAlongWindDirection = 400.0f;
					layerSecond.totalNoiseScale = 0.0006f;
					layerSecond.curlScale = 0.1f;
					layerSecond.curlNoiseHeightFraction = 500.0f;
					layerSecond.curlNoiseModifier = 250.0f;
					layerSecond.detailScale = 2.0f;
					layerSecond.detailNoiseHeightFraction = 0.0f;
					layerSecond.detailNoiseModifier = 1.0f;
					layerSecond.skewAlongCoverageWindDirection = 0.0f;
					layerSecond.weatherScale = 0.000025f;
					layerSecond.coverageAmount = 0.0f;
					layerSecond.coverageMinimum = 0.0f;
					layerSecond.typeAmount = 1.0f;
					layerSecond.typeMinimum = 0.0f;
					layerSecond.rainAmount = 0.0f; // Rain clouds disabled by default.
					layerSecond.rainMinimum = 0.0f;

					// Cloud types: 4 positions of a black, white, white, black gradient
					layerSecond.gradientSmall = XMFLOAT4(0.6f, 0.62f, 0.63f, 0.65f);
					layerSecond.gradientMedium = XMFLOAT4(0.6f, 0.64f, 0.66f, 0.7f);
					layerSecond.gradientLarge = XMFLOAT4(0.6f, 0.66f, 0.69f, 0.75f);

					// amountTop, offsetTop, amountBot, offsetBot: Control with 'amount' the coverage scale along the current gradient height, and can be adjusted with 'offset'
					layerSecond.anvilDeformationSmall = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
					layerSecond.anvilDeformationMedium = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);
					layerSecond.anvilDeformationLarge = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.0f);

					// Animation
					layerSecond.windSpeed = 10.0f;
					layerSecond.windAngle = 1.0f;
					layerSecond.windUpAmount = 0.1f;
					layerSecond.coverageWindSpeed = 50.0f;
					layerSecond.coverageWindAngle = 1.0f;
				}

				// Animation
				animationMultiplier = 2.0f;

				// Performance
				maxStepCount = 96;
				maxMarchingDistance = 30000.0f;
				inverseDistanceStepCount = 15000.0f;
				renderDistance = 70000.0f;
				LODDistance = 30000.0f;
				LODMin = 0.0f;
				bigStepMarch = 2.0f;
				transmittanceThreshold = 0.005f;
				shadowSampleCount = 5.0f;
				groundContributionSampleCount = 3.0f;
			}

			VolumetricCloudParameters() { Init(); }
		};

	protected:
		uint32_t flag_ = EMPTY;

		XMFLOAT3 sunColor_ = XMFLOAT3(0, 0, 0);
		XMFLOAT3 sunDirection_ = XMFLOAT3(0, 0, -1);
		float skyExposure_ = 1.f;
		XMFLOAT3 horizon_ = XMFLOAT3(0.0f, 0.0f, 0.0f);
		XMFLOAT3 zenith_ = XMFLOAT3(0.0f, 0.0f, 0.0f);
		XMFLOAT3 ambient_ = XMFLOAT3(0.2f, 0.2f, 0.2f);
		float fogStart_ = 100.f;
		float fogDensity_ = 0;
		float fogHeightStart_ = 1;
		float fogHeightEnd_ = 3;
		XMFLOAT3 windDirection_ = XMFLOAT3(0, 0, 0);
		float windRandomness_ = 5.f;
		float windWaveSize_ = 1.f;
		float windSpeed_ = 1.f;
		float stars_ = 0.5f;
		XMFLOAT3 gravity_ = XMFLOAT3(0, 0, -10.f);
		float skyRotation_ = 0; // horizontal rotation for skyMap texture (in radians)

		float rainAmount_ = 0;
		float rainLength_ = 0.04f;
		float rainSpeed_ = 1;
		float rainScale_ = 0.005f;
		float rainSplashScale_ = 0.1f;
		XMFLOAT4 rainColor_ = XMFLOAT4(0.6f, 0.8f, 1, 0.5f);

		OceanParameters oceanParameters_;
		AtmosphereParameters atmosphereParameters_;
		VolumetricCloudParameters volumetricCloudParameters_;

		// default is from filename
		std::string skyMapName_;
		std::string colorGradingMapName_;
		std::string volumetricCloudsWeatherMapFirstName_;
		std::string volumetricCloudsWeatherMapSecondName_;

		// Non-serialized attributes:
	public:
		EnvironmentComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::ENVIRONMENT, entity, vuid) {}
		virtual ~EnvironmentComponent() = default;

		// Non-serialized attributes:
		XMFLOAT4 starsRotationQuaternion = XMFLOAT4(0, 0, 0, 1);
		uint32_t mostImportantLightIndex = ~0u;

		inline bool IsOceanEnabled() const { return flag_ & OCEAN_ENABLED; }
		inline bool IsRealisticSky() const { return flag_ & REALISTIC_SKY; }
		inline bool IsVolumetricClouds() const { return flag_ & VOLUMETRIC_CLOUDS; }
		inline bool IsHeightFog() const { return flag_ & HEIGHT_FOG; }
		inline bool IsVolumetricCloudsCastShadow() const { return flag_ & VOLUMETRIC_CLOUDS_CAST_SHADOW; }
		inline bool IsOverrideFogColor() const { return flag_ & OVERRIDE_FOG_COLOR; }
		inline bool IsRealisticSkyAerialPerspective() const { return flag_ & REALISTIC_SKY_AERIAL_PERSPECTIVE; }
		inline bool IsRealisticSkyHighQuality() const { return flag_ & REALISTIC_SKY_HIGH_QUALITY; }
		inline bool IsRealisticSkyReceiveShadow() const { return flag_ & REALISTIC_SKY_RECEIVE_SHADOW; }
		inline bool IsVolumetricCloudsReceiveShadow() const { return flag_ & VOLUMETRIC_CLOUDS_RECEIVE_SHADOW; }

		inline void SetOceanEnabled(bool value = true) { if (value) { flag_ |= OCEAN_ENABLED; } else { flag_ &= ~OCEAN_ENABLED; } }
		inline void SetRealisticSky(bool value = true) { if (value) { flag_ |= REALISTIC_SKY; } else { flag_ &= ~REALISTIC_SKY; } }
		inline void SetVolumetricClouds(bool value = true) { if (value) { flag_ |= VOLUMETRIC_CLOUDS; } else { flag_ &= ~VOLUMETRIC_CLOUDS; } }
		inline void SetHeightFog(bool value = true) { if (value) { flag_ |= HEIGHT_FOG; } else { flag_ &= ~HEIGHT_FOG; } }
		inline void SetVolumetricCloudsCastShadow(bool value = true) { if (value) { flag_ |= VOLUMETRIC_CLOUDS_CAST_SHADOW; } else { flag_ &= ~VOLUMETRIC_CLOUDS_CAST_SHADOW; } }
		inline void SetOverrideFogColor(bool value = true) { if (value) { flag_ |= OVERRIDE_FOG_COLOR; } else { flag_ &= ~OVERRIDE_FOG_COLOR; } }
		inline void SetRealisticSkyAerialPerspective(bool value = true) { if (value) { flag_ |= REALISTIC_SKY_AERIAL_PERSPECTIVE; } else { flag_ &= ~REALISTIC_SKY_AERIAL_PERSPECTIVE; } }
		inline void SetRealisticSkyHighQuality(bool value = true) { if (value) { flag_ |= REALISTIC_SKY_HIGH_QUALITY; } else { flag_ &= ~REALISTIC_SKY_HIGH_QUALITY; } }
		inline void SetRealisticSkyReceiveShadow(bool value = true) { if (value) { flag_ |= REALISTIC_SKY_RECEIVE_SHADOW; } else { flag_ &= ~REALISTIC_SKY_RECEIVE_SHADOW; } }
		inline void SetVolumetricCloudsReceiveShadow(bool value = true) { if (value) { flag_ |= VOLUMETRIC_CLOUDS_RECEIVE_SHADOW; } else { flag_ &= ~VOLUMETRIC_CLOUDS_RECEIVE_SHADOW; } }

		// Getters
		inline const XMFLOAT3& GetSunColor() const { return sunColor_; }
		inline const XMFLOAT3& GetSunDirection() const { return sunDirection_; }
		inline float GetSkyExposure() const { return skyExposure_; }
		inline const XMFLOAT3& GetHorizon() const { return horizon_; }
		inline const XMFLOAT3& GetZenith() const { return zenith_; }
		inline const XMFLOAT3& GetAmbient() const { return ambient_; }
		inline float GetFogStart() const { return fogStart_; }
		inline float GetFogDensity() const { return fogDensity_; }
		inline float GetFogHeightStart() const { return fogHeightStart_; }
		inline float GetFogHeightEnd() const { return fogHeightEnd_; }
		inline const XMFLOAT3& GetWindDirection() const { return windDirection_; }
		inline float GetWindRandomness() const { return windRandomness_; }
		inline float GetWindWaveSize() const { return windWaveSize_; }
		inline float GetWindSpeed() const { return windSpeed_; }
		inline float GetStars() const { return stars_; }
		inline const XMFLOAT3& GetGravity() const { return gravity_; }
		inline float GetSkyRotation() const { return skyRotation_; }
		inline float GetRainAmount() const { return rainAmount_; }
		inline float GetRainLength() const { return rainLength_; }
		inline float GetRainSpeed() const { return rainSpeed_; }
		inline float GetRainScale() const { return rainScale_; }
		inline float GetRainSplashScale() const { return rainSplashScale_; }
		inline const XMFLOAT4& GetRainColor() const { return rainColor_; }
		inline const OceanParameters& GetOceanParameters() const { return oceanParameters_; }
		inline const AtmosphereParameters& GetAtmosphereParameters() const { return atmosphereParameters_; }
		inline const VolumetricCloudParameters& GetVolumetricCloudParameters() const { return volumetricCloudParameters_; }
		inline OceanParameters& GetMutableOceanParameters() { return oceanParameters_; timeStampSetter_ = TimerNow; }
		inline AtmosphereParameters& GetMutableAtmosphereParameters() { return atmosphereParameters_; timeStampSetter_ = TimerNow; }
		inline VolumetricCloudParameters& GetMutableVolumetricCloudParameters() { return volumetricCloudParameters_; timeStampSetter_ = TimerNow; }
		inline const std::string& GetSkyMapName() const { return skyMapName_; }
		inline const std::string& GetColorGradingMapName() const { return colorGradingMapName_; }
		inline const std::string& GetVolumetricCloudsWeatherMapFirstName() const { return volumetricCloudsWeatherMapFirstName_; }
		inline const std::string& GetVolumetricCloudsWeatherMapSecondName() const { return volumetricCloudsWeatherMapSecondName_; }

		// Setters
		inline void SetSunColor(const XMFLOAT3& sunColor) { sunColor_ = sunColor; timeStampSetter_ = TimerNow; }
		inline void SetSunDirection(const XMFLOAT3& sunDirection) { sunDirection_ = sunDirection; timeStampSetter_ = TimerNow; }
		inline void SetSkyExposure(const float skyExposure) { skyExposure_ = skyExposure; timeStampSetter_ = TimerNow; }
		inline void SetHorizon(const XMFLOAT3& horizon) { horizon_ = horizon; timeStampSetter_ = TimerNow; }
		inline void SetZenith(const XMFLOAT3& zenith) { zenith_ = zenith; timeStampSetter_ = TimerNow; }
		inline void SetAmbient(const XMFLOAT3& ambient) { ambient_ = ambient; timeStampSetter_ = TimerNow; }
		inline void SetFogStart(const float fogStart) { fogStart_ = fogStart; timeStampSetter_ = TimerNow; }
		inline void SetFogDensity(const float fogDensity) { fogDensity_ = fogDensity; timeStampSetter_ = TimerNow; }
		inline void SetFogHeightStart(const float fogHeightStart) { fogHeightStart_ = fogHeightStart; timeStampSetter_ = TimerNow; }
		inline void SetFogHeightEnd(const float fogHeightEnd) { fogHeightEnd_ = fogHeightEnd; timeStampSetter_ = TimerNow; }
		inline void SetWindDirection(const XMFLOAT3& windDirection) { windDirection_ = windDirection; timeStampSetter_ = TimerNow; }
		inline void SetWindRandomness(const float windRandomness) { windRandomness_ = windRandomness; timeStampSetter_ = TimerNow; }
		inline void SetWindWaveSize(const float windWaveSize) { windWaveSize_ = windWaveSize; timeStampSetter_ = TimerNow; }
		inline void SetWindSpeed(const float windSpeed) { windSpeed_ = windSpeed; timeStampSetter_ = TimerNow; }
		inline void SetStars(const float stars) { stars_ = stars; timeStampSetter_ = TimerNow; }
		inline void SetGravity(const XMFLOAT3& gravity) { gravity_ = gravity; timeStampSetter_ = TimerNow; }
		inline void SetSkyRotation(const float skyRotation) { skyRotation_ = skyRotation; timeStampSetter_ = TimerNow; }
		inline void SetRainAmount(const float rainAmount) { rainAmount_ = rainAmount; timeStampSetter_ = TimerNow; }
		inline void SetRainLength(const float rainLength) { rainLength_ = rainLength; timeStampSetter_ = TimerNow; }
		inline void SetRainSpeed(const float rainSpeed) { rainSpeed_ = rainSpeed; timeStampSetter_ = TimerNow; }
		inline void SetRainScale(const float rainScale) { rainScale_ = rainScale; timeStampSetter_ = TimerNow; }
		inline void SetRainSplashScale(const float rainSplashScale) { rainSplashScale_ = rainSplashScale; timeStampSetter_ = TimerNow; }
		inline void SetRainColor(const XMFLOAT4& rainColor) { rainColor_ = rainColor; timeStampSetter_ = TimerNow; }
		inline void SetOceanParameters(const OceanParameters& oceanParameters) { oceanParameters_ = oceanParameters; timeStampSetter_ = TimerNow; }
		inline void SetAtmosphereParameters(const AtmosphereParameters& atmosphereParameters) { atmosphereParameters_ = atmosphereParameters; timeStampSetter_ = TimerNow; }
		inline void SetVolumetricCloudParameters(const VolumetricCloudParameters& volumetricCloudParameters) { volumetricCloudParameters_ = volumetricCloudParameters; timeStampSetter_ = TimerNow; }
		
		virtual bool LoadSkyMap(const std::string& fileName) = 0;
		virtual bool LoadColorGradingMap(const std::string& fileName) = 0;
		virtual bool LoadVolumetricCloudsWeatherMapFirst(const std::string& fileName) = 0;
		virtual bool LoadVolumetricCloudsWeatherMapSecond(const std::string& fileName) = 0;

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::ENVIRONMENT;
	};

	struct CORE_EXPORT ProbeComponent : ComponentBase
	{
		static constexpr uint32_t envmapMSAASampleCount = 8;
		enum FLAGS
		{
			EMPTY = 0,
			PERFRAME_UPDATE = 1 << 0, // force to lower quality rendering
			MSAA = 1 << 1,
		};
	protected:
		uint32_t flags_ = EMPTY;
		uint32_t resolution_ = 128; // power of two
		std::string textureName_; // if texture is coming from an asset

	public:
		ProbeComponent(const Entity entity, const VUID vuid = 0) : ComponentBase(ComponentType::PROBE, entity, vuid) {}
		virtual ~ProbeComponent() = default;

		// Non-serialized attributes: (read only, DO NOT modify)
		// which will be updated by TransformUpdates (world space) during scene updates
		XMFLOAT3 position;
		float range;
		XMFLOAT4X4 inverseMatrix; // world to local (probe), i.e., ws2os

		inline void SetUpdatePerFrameEnabled(const bool value) { if (value) { flags_ |= PERFRAME_UPDATE; } else { flags_ &= ~PERFRAME_UPDATE; } timeStampSetter_ = TimerNow; }
		inline void SetMSAA(const bool value) { if (value) { flags_ |= MSAA; } else { flags_ &= ~MSAA; } timeStampSetter_ = TimerNow; }
		inline void SetResolution(const uint32_t resolution) { resolution_ = math::GetNextPowerOfTwo(resolution); timeStampSetter_ = TimerNow; }

		inline bool IsRealTime() const { return flags_ & PERFRAME_UPDATE; }
		inline bool IsMSAA() const { return flags_ & MSAA; }
		inline uint32_t GetResolution() const { return resolution_; };
		inline uint32_t GetSampleCount() const { return IsMSAA() ? envmapMSAASampleCount : 1; }

		virtual bool LoadTexture(const std::string& fileName) = 0;
		virtual void RemoveTexture() = 0;

		size_t GetMemorySizeInBytes() const;

		void Serialize(vz::Archive& archive, const uint64_t version) override;

		inline static const ComponentType IntrinsicType = ComponentType::PROBE;
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
	CORE_EXPORT ComponentBase* GetComponentByVUID(const VUID vuid);
	CORE_EXPORT Entity GetEntityByVUID(const VUID vuid);
	inline ComponentType GetCompTypeFromVUID(VUID vuid) { return static_cast<ComponentType>(uint32_t(vuid & 0xFF)); }

	// Component Manager
	CORE_EXPORT size_t SetSceneComponentsDirty(const Entity entity);

	CORE_EXPORT NameComponent* GetNameComponent(const Entity entity);
	CORE_EXPORT TransformComponent* GetTransformComponent(const Entity entity);
	CORE_EXPORT HierarchyComponent* GetHierarchyComponent(const Entity entity);
	CORE_EXPORT LayeredMaskComponent* GetLayeredMaskComponent(const Entity entity);
	CORE_EXPORT ColliderComponent* GetColliderComponent(const Entity entity);
	CORE_EXPORT MaterialComponent* GetMaterialComponent(const Entity entity);
	CORE_EXPORT GeometryComponent* GetGeometryComponent(const Entity entity);
	CORE_EXPORT TextureComponent* GetTextureComponent(const Entity entity);
	CORE_EXPORT VolumeComponent* GetVolumeComponent(const Entity entity);
	CORE_EXPORT RenderableComponent* GetRenderableComponent(const Entity entity);
	CORE_EXPORT SpriteComponent* GetSpriteComponent(const Entity entity);
	CORE_EXPORT SpriteFontComponent* GetSpriteFontComponent(const Entity entity);
	CORE_EXPORT LightComponent* GetLightComponent(const Entity entity);
	CORE_EXPORT CameraComponent* GetCameraComponent(const Entity entity);
	CORE_EXPORT SlicerComponent* GetSlicerComponent(const Entity entity);
	CORE_EXPORT EnvironmentComponent* GetEnvironmentComponent(const Entity entity);
	CORE_EXPORT ProbeComponent* GetProbeComponent(const Entity entity);

	CORE_EXPORT NameComponent* GetNameComponentByVUID(const VUID vuid);
	CORE_EXPORT TransformComponent* GetTransformComponentByVUID(const VUID vuid);
	CORE_EXPORT HierarchyComponent* GetHierarchyComponentByVUID(const VUID vuid);
	CORE_EXPORT ColliderComponent* GetColliderComponentByVUID(const VUID vuid);
	CORE_EXPORT MaterialComponent* GetMaterialComponentByVUID(const VUID vuid);
	CORE_EXPORT GeometryComponent* GetGeometryComponentByVUID(const VUID vuid);
	CORE_EXPORT TextureComponent* GetTextureComponentByVUID(const VUID vuid);
	CORE_EXPORT VolumeComponent* GetVolumeComponentByVUID(const VUID vuid);
	CORE_EXPORT RenderableComponent* GetRenderableComponentByVUID(const VUID vuid);
	CORE_EXPORT LightComponent* GetLightComponentByVUID(const VUID vuid);
	CORE_EXPORT CameraComponent* GetCameraComponentByVUID(const VUID vuid);
	CORE_EXPORT SlicerComponent* GetSlicerComponentByVUID(const VUID vuid);
	CORE_EXPORT EnvironmentComponent* GetEnvironmentComponentByVUID(const VUID vuid);
	CORE_EXPORT ProbeComponent* GetProbeComponentByVUID(const VUID vuid);

	CORE_EXPORT bool ContainNameComponent(const Entity entity);
	CORE_EXPORT bool ContainTransformComponent(const Entity entity);
	CORE_EXPORT bool ContainHierarchyComponent(const Entity entity);
	CORE_EXPORT bool ContainLayeredMaskComponent(const Entity entity);
	CORE_EXPORT bool ContainColliderComponent(const Entity entity);
	CORE_EXPORT bool ContainMaterialComponent(const Entity entity);
	CORE_EXPORT bool ContainGeometryComponent(const Entity entity);
	CORE_EXPORT bool ContainRenderableComponent(const Entity entity);
	CORE_EXPORT bool ContainSpriteComponent(const Entity entity);
	CORE_EXPORT bool ContainSpriteFontComponent(const Entity entity);
	CORE_EXPORT bool ContainLightComponent(const Entity entity);
	CORE_EXPORT bool ContainCameraComponent(const Entity entity);
	CORE_EXPORT bool ContainSlicerComponent(const Entity entity);
	CORE_EXPORT bool ContainTextureComponent(const Entity entity);
	CORE_EXPORT bool ContainVolumeComponent(const Entity entity);
	CORE_EXPORT bool ContainEnvironmentComponent(const Entity entity);
	CORE_EXPORT bool ContainProbeComponent(const Entity entity);

	CORE_EXPORT size_t GetComponents(const Entity entity, std::vector<ComponentBase*>& components);
	CORE_EXPORT size_t GetEntitiesByName(const std::string& name, std::vector<Entity>& entities); // when there is a name component
	CORE_EXPORT Entity GetFirstEntityByName(const std::string& name);

	CORE_EXPORT void EntitySafeExecute(const std::function<void(const std::vector<Entity>&)>& task, const std::vector<Entity>& entities);	// this is for engine-thread safe call

	//----- Highlevel APIs-mapping -----//
	// To create ECS-based components, 
	//	engine developers are restricted to using node-based components 
	//	(which are composed of groups of ECS-based components) defined in high-level APIs
	// ** NOTE: Only Engine Framework owners are allowed to create specific ECS-based components
	CORE_EXPORT Entity MakeNodeActor(const std::string& name, const Entity parentEntity = 0ull);
	CORE_EXPORT Entity MakeNodeStaticMeshActor(const std::string& name, const Entity parentEntity = 0ull);
	CORE_EXPORT Entity MakeNodeSpriteActor(const std::string& name, const Entity parentEntity = 0ull);
	CORE_EXPORT Entity MakeNodeSpriteFontActor(const std::string& name, const Entity parentEntity = 0ull);
	CORE_EXPORT Entity MakeNodeCamera(const std::string& name, const Entity parentEntity = 0ull);
	CORE_EXPORT Entity MakeNodeSlicer(const std::string& name, const bool curvedSlicer, const Entity parentEntity = 0ull);
	CORE_EXPORT Entity MakeNodeLight(const std::string& name, const Entity parentEntity = 0ull);
	CORE_EXPORT Entity MakeResGeometry(const std::string& name);
	CORE_EXPORT Entity MakeResMaterial(const std::string& name);
	CORE_EXPORT Entity MakeResTexture(const std::string& name);
	CORE_EXPORT Entity MakeResVolume(const std::string& name);
	CORE_EXPORT size_t RemoveEntity(const Entity entity, const bool includeDescendants = false); // Only ECS components
}
