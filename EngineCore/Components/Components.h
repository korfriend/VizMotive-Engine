#pragma once
#include "VzEnums.h"
#include "Libs/Math.h"
#include "Libs/PrimitiveHelper.h"

#include <vector>
#include <map>
#include <unordered_set>
#include <string>
#include <memory>
#include <chrono>

#ifdef _WIN32
#define CORE_EXPORT __declspec(dllexport)
#else
#define CORE_EXPORT __attribute__((visibility("default")))
#endif

using Entity = uint32_t;
using VUID = uint64_t;
inline constexpr Entity INVALID_ENTITY = 0;
inline constexpr VUID INVALID_VUID = 0;
using TimeStamp = std::chrono::high_resolution_clock::time_point;
#define TimeDurationCount(A, B) std::chrono::duration_cast<std::chrono::duration<double>>(A - B).count()
#define TimerNow std::chrono::high_resolution_clock::now()
#define TimerMin std::chrono::high_resolution_clock::time_point::min();

namespace vz
{
	// core...
	// create components...
	// create scene...
	// resource???!
}

namespace vz::resource
{

	// This can hold an asset
	//	It can be loaded from file or memory using vz::resourcemanager::Load()
	struct CORE_EXPORT Resource
	{
		std::shared_ptr<void> internal_state;

		inline bool IsValid() const { return internal_state.get() != nullptr; }

		const std::vector<uint8_t>& GetFileData() const;
		const std::string& GetScript() const;
		size_t GetScriptHash() const;
		int GetTextureSRGBSubresource() const;
		int GetFontStyle() const;

		void SetFileData(const std::vector<uint8_t>& data);
		void SetFileData(std::vector<uint8_t>&& data);

		// Resource marked for recreate on resourcemanager::Load()
		void SetOutdated();

		// Let the streaming system know the required resolution of this resource
		void StreamingRequestResolution(uint32_t resolution);
	};
}

namespace vz
{
	class Archive;
	namespace ecs { struct EntitySerializer; }

	struct CORE_EXPORT Scene
	{
	private:
		inline static std::map<Entity, Scene*> scenes_;
	public:
		static Scene* GetScene(const Entity entity) {
			auto it = scenes_.find(entity);
			return it != scenes_.end() ? it->second : nullptr;
		}
		static Scene* GetFirstSceneByName(const std::string& name) {
			for (auto& it : scenes_) {
				if (it.second->sceneName == name) return it.second;
			}
			return nullptr;
		}
		static Scene* CreateScene(const std::string& name);

	private:
		Entity entity_ = INVALID_ENTITY;
		std::string name_;
		std::vector<Entity> renderables_;
		std::vector<Entity> lights_;
		std::unordered_map<size_t, Entity> renderableMap_; // each entity has also TransformComponent and HierarchyComponent
		std::unordered_map<size_t, Entity> lightMap_;

		// Non-serialized attributes:
		TimeStamp recentRenderTime_ = TimerMin;	// world update time

	public:

		void AddEntity(const Entity entity);

		/**
		 * Adds a list of entities to the Scene.
		 *
		 * @param entities Array containing entities to add to the scene.
		 * @param count Size of the entity array.
		 */
		void AddEntities(const std::vector<Entity>& entities);

		/**
		 * Removes the Renderable from the Scene.
		 *
		 * @param entity The Entity to remove from the Scene. If the specified
		 *                   \p entity doesn't exist, this call is ignored.
		 */
		void Remove(const Entity entity);

		/**
		 * Removes a list of entities to the Scene.
		 *
		 * This is equivalent to calling remove in a loop.
		 * If any of the specified entities do not exist in the scene, they are skipped.
		 *
		 * @param entities Array containing entities to remove from the scene.
		 * @param count Size of the entity array.
		 */
		void RemoveEntities(const std::vector<Entity>& entities);

		/**
		 * Returns the total number of Entities in the Scene, whether alive or not.
		 * @return Total number of Entities in the Scene.
		 */
		size_t GetEntityCount() const noexcept;

		/**
		 * Returns the number of active (alive) Renderable objects in the Scene.
		 *
		 * @return The number of active (alive) Renderable objects in the Scene.
		 */
		size_t GetRenderableCount() const noexcept;

		/**
		 * Returns the number of active (alive) Light objects in the Scene.
		 *
		 * @return The number of active (alive) Light objects in the Scene.
		 */
		size_t GetLightCount() const noexcept;

		/**
		 * Returns true if the given entity is in the Scene.
		 *
		 * @return Whether the given entity is in the Scene.
		 */
		bool HasEntity(const Entity entity) const noexcept;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT ComponentBase
	{
	protected:
		std::string componentType_ = "UNDEF";
		TimeStamp timeStampSetter_ = TimerMin;
		Entity entity_ = INVALID_ENTITY;
		VUID vuid_ = INVALID_VUID;	 
	public:
		ComponentBase(const std::string& typeName, const Entity entity) : componentType_(typeName), entity_(entity) {
			timeStampSetter_ = TimerNow;
		}
		std::string GetComponentType() const { return componentType_; }
		TimeStamp GetTimeStamp() const { return timeStampSetter_; }
		Entity GetEntity() const { return entity_; }
		VUID GetVUID() const { return vuid_; }
	};

	struct CORE_EXPORT NameComponent : ComponentBase
	{
		NameComponent(const Entity entity) : ComponentBase("NameComponent", entity) {}

		std::string name;

		inline void operator=(const std::string& str) { name = str; }
		inline void operator=(std::string&& str) { name = std::move(str); }
		inline bool operator==(const std::string& str) const { return name.compare(str) == 0; }

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
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
		XMFLOAT4X4 local_ = vz::math::IDENTITY_MATRIX;

		// check timeStampWorldUpdate_ and global timeStamp
		TimeStamp timeStampWorldUpdate_ = TimerMin;
		XMFLOAT4X4 world_ = vz::math::IDENTITY_MATRIX;

	public:
		TransformComponent(const Entity entity) : ComponentBase("TransformComponent", entity) {}

		bool IsDirty() const { return isDirty_; }
		bool IsMatrixAutoUpdate() const { return isMatrixAutoUpdate_; }

		// recommend checking IsDirtyWorldMatrix with scene's timeStampWorldUpdate
		XMFLOAT3 GetWorldPosition() const;
		XMFLOAT4 GetWorldRotation() const;
		XMFLOAT3 GetWorldScale() const;
		XMFLOAT3 GetWorldForward() const; // z-axis
		XMFLOAT3 GetWorldUp() const;
		XMFLOAT3 GetWorldRight() const;
		XMFLOAT4X4 GetWorldMatrix() const { return world_; };

		// Local
		XMFLOAT3 GetPosition() const { return position_; };
		XMFLOAT4 GetRotation() const { return rotation_; };
		XMFLOAT3 GetScale() const { return scale_; };
		XMFLOAT4X4 GetLocalMatrix() const { return local_; };

		void SetPosition(const XMFLOAT3& p) { isDirty_ = true; position_ = p; timeStampSetter_ = TimerNow; }
		void SetScale(const XMFLOAT3& s) { isDirty_ = true; scale_ = s; timeStampSetter_ = TimerNow; }
		void SetEulerAngleZXY(const XMFLOAT3& rotAngles); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetEulerAngleZXYInDegree(const XMFLOAT3& rotAngles); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetQuaternion(const XMFLOAT4& q) { isDirty_ = true; rotation_ = q; timeStampSetter_ = TimerNow; }
		void SetMatrix(const XMFLOAT4X4& local);

		void SetWorldMatrix(const XMFLOAT4X4& world) { world_ = world; timeStampWorldUpdate_ = TimerNow; };

		void UpdateMatrix();	// local matrix
		void UpdateWorldMatrix(); // call UpdateMatrix() if necessary
		bool IsDirtyWorldMatrix(const TimeStamp timeStampRecentWorldUpdate) { return TimeDurationCount(timeStampRecentWorldUpdate, timeStampWorldUpdate_) <= 0; }

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT HierarchyComponent : ComponentBase
	{
		HierarchyComponent(const Entity entity) : ComponentBase("HierarchyComponent", entity) {}

		Entity parentEntity = INVALID_ENTITY;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// resources
	struct CORE_EXPORT MaterialComponent : ComponentBase
	{
	public:
		enum class RenderFlags : uint32_t
		{
			DAFAULT = 1 << 0,
			USE_VERTEXCOLORS = 1 << 1,
			SPECULAR_GLOSSINESS_WORKFLOW = 1 << 2,
			DOUBLE_SIDED = 1 << 3,
			OUTLINE = 1 << 4,
			FORWARD = 1 << 5 // "not forward" refers to "deferred"
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

	private:
		bool isDirty_ = true;
		uint32_t renderOptionFlags_ = (uint32_t)RenderFlags::FORWARD;

		ShaderType shaderType = ShaderType::PHONG;

		XMFLOAT4 baseColor_ = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 specularColor_ = XMFLOAT4(1, 1, 1, 1);
		XMFLOAT4 emissiveColor_ = XMFLOAT4(1, 1, 1, 0);

		XMFLOAT4 phongFactors_ = XMFLOAT4(0.2f, 1, 1, 1);	// only used for ShaderType::PHONG

		Entity textures_[SCU32(TextureSlot::TEXTURESLOT_COUNT)] = {};
	public:
		MaterialComponent(const Entity entity) : ComponentBase("MaterialComponent", entity) {}

		XMFLOAT4 GetBaseColor() { return baseColor_; }	// w is opacity
		XMFLOAT4 GetSpecularColor() { return specularColor_; }
		XMFLOAT4 GetEmissiveColor() { return emissiveColor_; }	// w is emissive strength

		XMFLOAT4 SetBaseColor(const XMFLOAT4& baseColor) { baseColor_ = baseColor; isDirty_ = true; }	
		XMFLOAT4 SetSpecularColor(const XMFLOAT4& specularColor) { specularColor_ = specularColor; isDirty_ = true; }
		XMFLOAT4 SetEmissiveColor(const XMFLOAT4& emissiveColor) { emissiveColor_ = emissiveColor; isDirty_ = true; }	

		bool IsDirty() const { return isDirty_; }

		// Create texture resources for GPU
		void GpuUpdateAssociatedTextures();

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT GeometryComponent : ComponentBase
	{
	private:
		struct Primitive {
		private:
			inline static const size_t numBuffers_ = 6;
			bool isValid_[numBuffers_] = { }; // false

			std::vector<XMFLOAT3> vertexPositions_;
			std::vector<XMFLOAT3> vertexNormals_;
			std::vector<XMFLOAT2> vertexUVset0_;
			std::vector<XMFLOAT2> vertexUVset1_;
			std::vector<uint32_t> vertexColors_;
			std::vector<uint32_t> indexPrimitives_;

			geometry::AABB aabb_;
			enums::PrimitiveType ptype_ = enums::PrimitiveType::TRIANGLES;
		public:
			bool IsValid() const { for (size_t i = 0; i < numBuffers_; ++i) { if (!isValid_[i]) return false; } return true; }
			void MoveFrom(const Primitive& primitive)
			{
				vertexPositions_ = std::move(primitive.vertexPositions_);
				vertexNormals_ = std::move(primitive.vertexNormals_);
				vertexUVset0_ = std::move(primitive.vertexUVset0_);
				vertexUVset1_ = std::move(primitive.vertexUVset1_);
				vertexColors_ = std::move(primitive.vertexColors_);
				indexPrimitives_ = std::move(primitive.indexPrimitives_);
				aabb_ = primitive.aabb_;
				ptype_ = primitive.ptype_;
				for (size_t i = 0; i < numBuffers_; ++i) isValid_[i] = true;
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
				for (size_t i = 0; i < numBuffers_; ++i) isValid_[i] = false;
			}
			geometry::AABB GetAABB() { return aabb_; }
			enums::PrimitiveType GetPrimitiveType() { return ptype_; }
			void SetAABB(const geometry::AABB& aabb) { aabb_ = aabb; }
			void SetPrimitiveType(const enums::PrimitiveType ptype) { ptype_ = ptype; }
#define PRIM_GETTER(A)  if (data) { data = A.data(); } return A.size();
			size_t GetVtxPositions(XMFLOAT3* data) { assert(isValid_[0]); PRIM_GETTER(vertexPositions_) }
			size_t GetVtxNormals(XMFLOAT3* data) { assert(isValid_[1]); PRIM_GETTER(vertexNormals_) }
			size_t GetVtxUVSet0(XMFLOAT2* data) { assert(isValid_[2]); PRIM_GETTER(vertexUVset0_) }
			size_t GetVtxUVSet1(XMFLOAT2* data) { assert(isValid_[3]); PRIM_GETTER(vertexUVset1_) }
			size_t GetVtxColors(uint32_t* data) { assert(isValid_[4]); PRIM_GETTER(vertexColors_) }
			size_t GetIdxPrimives(uint32_t* data) { assert(isValid_[5]); PRIM_GETTER(indexPrimitives_) }
#define PRIM_SETTER(A, B) A##_ = onlyMoveOwnership ? std::move(A) : A; isValid_[B] = true;
			void SetVtxPositions(const std::vector<XMFLOAT3>& vertexPositions, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexPositions, 0) }
			void SetVtxNormals(const std::vector<XMFLOAT3>& vertexNormals, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexNormals, 1) }
			void SetVtxUVSet0(const std::vector<XMFLOAT2>& vertexUVset0, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexUVset0, 2) }
			void SetVtxUVSet1(const std::vector<XMFLOAT2>& vertexUVset1, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexUVset1, 3) }
			void SetVtxColors(const std::vector<uint32_t>& vertexColors, const bool onlyMoveOwnership = false) { PRIM_SETTER(vertexColors, 4) }
			void SetIdxPrimives(const std::vector<uint32_t>& indexPrimitives, const bool onlyMoveOwnership = false) { PRIM_SETTER(indexPrimitives, 5) }

			void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
		};

		std::vector<Primitive> parts_;

		bool isDirty_ = true;
		geometry::AABB aabb_; // not serialized (automatically updated)

		void updateAABB();
	public:
		GeometryComponent(const Entity entity) : ComponentBase("GeometryComponent", entity) {}

		bool IsDirty() { return isDirty_; }
		geometry::AABB GetAABB() { return aabb_; }
		void AssignParts(const size_t numParts) { parts_.assign(numParts, Primitive()); }
		void MovePrimitives(const std::vector<Primitive>& primitives);
		void CopyPrimitives(const std::vector<Primitive>& primitives);
		void MovePrimitive(const Primitive& primitive, const size_t slot);
		void CopyPrimitive(const Primitive& primitive, const size_t slot);
		bool GetPrimitive(const size_t slot, Primitive& primitive);
		size_t GetNumParts() { return parts_.size(); }
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT TextureComponent : ComponentBase
	{
		TextureComponent(const Entity entity) : ComponentBase("TextureComponent", entity) {}
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// scene 
	struct CORE_EXPORT RenderableComponent : ComponentBase
	{
	private:
		uint8_t visibleLayerMask_ = 0x7;
		Entity geometryEntity_ = INVALID_ENTITY;
		std::vector<Entity> materialEntities_;

		// Non-serialized attributes:
		bool isValid_ = false;
	public:
		RenderableComponent(const Entity entity) : ComponentBase("RenderableComponent", entity) {}

		// Non-serialized attributes: (those variables are supposed to be updated via transformers)
		XMFLOAT4X4 matWorld;

		bool IsValid() { return isValid_; }
		void SetGeometry(const Entity geometryEntity);
		bool SetMaterial(const Entity materialEntity, const size_t slot);
		void SetMaterials(const std::vector<Entity>& materials);
		void SetVisibleMask(const uint8_t layerBits, const uint8_t maskBits) { visibleLayerMask_ = (layerBits & maskBits); }
		bool IsVisibleWith(uint8_t visibleLayerMask) { return visibleLayerMask & visibleLayerMask_; }
		uint8_t GetVisibleMask() const { return visibleLayerMask_; }

		Entity GetGeometry() { return geometryEntity_; }
		Entity GetMaterial(const size_t slot) { return slot >= materialEntities_.size() ? INVALID_ENTITY : materialEntities_[slot]; }
		std::vector<Entity> GetMaterials() { return materialEntities_; }
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT LightComponent : ComponentBase
	{
	private:
		uint32_t lightFlag_ = SCU32(enums::LightFlags::EMPTY);
		enums::LightType type_ = enums::LightType::DIRECTIONAL;

		XMFLOAT3 color_ = XMFLOAT3(1, 1, 1);

		// note there will be added many attributes to describe the light properties with various lighting techniques
		// refer to filament engine's lightManager and wicked engine's lightComponent
	public:
		LightComponent(const Entity entity) : ComponentBase("LightComponent", entity) {}

		// Non-serialized attributes:
		XMFLOAT4X4 matWorld;
		
		void SetLightColor(XMFLOAT3 color) { color_ = color; }

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT CameraComponent : ComponentBase
	{
	private:
		float zNearP_ = 0.1f;
		float zFarP_ = 5000.0f;
		float fovY_ = XM_PI / 3.0f;

		// These parameters are used differently depending on the projection mode.
		// 1. orthogonal : image plane's width and height
		// 2. perspective : computing aspect (W / H) ratio, i.e., (width_, height_) := (aspectRatio, 1.f)
		float width_ = 0.0f;
		float height_ = 0.0f;

		enums::Projection projectionType_ = enums::Projection::PERSPECTIVE;

		// Non-serialized attributes:
		bool isDirty_ = true;
		XMFLOAT3 eye_ = XMFLOAT3(0, 0, 0);
		XMFLOAT3 at_ = XMFLOAT3(0, 0, 1);
		XMFLOAT3 up_ = XMFLOAT3(0, 1, 0);
		XMFLOAT3X3 rotationMatrix_;
		XMFLOAT4X4 view_, projection_, viewProjection_;
		XMFLOAT4X4 invView_, invProjection_, invViewProjection_;
		vz::geometry::Frustum frustum_;

	public:
		CameraComponent(const Entity entity) : ComponentBase("CameraComponent", entity) {}

		// Non-serialized attributes:
		XMFLOAT2 jitter = XMFLOAT2(0, 0);

		bool IsDirty() const { return isDirty_; }

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
		vz::geometry::Frustum GetFrustum() const { return frustum_; }

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};
}

// component factory
namespace vz::compfactory
{
	NameComponent* CreateNameComponent(const Entity entity, const std::string& name);
	TransformComponent* CreateTransformComponent(const Entity entity);
	HierarchyComponent* CreateHierarchyComponent(const Entity entity, const Entity parent = INVALID_ENTITY);
	MaterialComponent* CreateMaterialComponent(const Entity entity);
	GeometryComponent* CreateGeometryComponent(const Entity entity);
	LightComponent* CreateLightComponent(const Entity entity);
	CameraComponent* CreateCameraComponent(const Entity entity);

	NameComponent* GetNameComponent(const Entity entity);
	TransformComponent* GetTransformComponent(const Entity entity);
	HierarchyComponent* GetHierarchyComponent(const Entity entity);
	MaterialComponent* GetMaterialComponent(const Entity entity);
	GeometryComponent* GetGeometryComponent(const Entity entity);
	LightComponent* GetLightComponent(const Entity entity);
	CameraComponent* GetCameraComponent(const Entity entity);

	bool ContainNameComponent(const Entity entity);
	bool ContainTransformComponent(const Entity entity);
	bool ContainHierarchyComponent(const Entity entity);
	bool ContainMaterialComponent(const Entity entity);
	bool ContainGeometryComponent(const Entity entity);
	bool ContainLightComponent(const Entity entity);
	bool ContainCameraComponent(const Entity entity);

	size_t GetComponents(const Entity entity, std::vector<ComponentBase*>& components);
	size_t GetComponentsByName(const std::string& name, std::vector<ComponentBase*>& components); // when there is a name component
}
