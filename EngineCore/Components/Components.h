#pragma once
#include "VzEnums.h"
#include "Libs/Math.h"
#include "Libs/PrimitiveHelper.h"

#include <vector>
#include <string>
#include <memory>

#ifdef _WIN32
#define CORE_EXPORT __declspec(dllexport)
#else
#define CORE_EXPORT __attribute__((visibility("default")))
#endif

using Entity = uint32_t;
inline constexpr Entity INVALID_ENTITY = 0;

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
	struct CORE_EXPORT Scene
	{
		// the directional light is always stored first in the LightSoA, so we need to account
		// for that in a few places.
		static constexpr size_t DIRECTIONAL_LIGHTS_COUNT = 1;
		/**
		 * Sets the Skybox.
		 *
		 * The Skybox is drawn last and covers all pixels not touched by geometry.
		 *
		 * @param skybox The Skybox to use to fill untouched pixels, or nullptr to unset the Skybox.
		 */
		 //void SetSkybox(Skybox* UTILS_NULLABLE skybox) noexcept;

		 /**
		  * Returns the Skybox associated with the Scene.
		  *
		  * @return The associated Skybox, or nullptr if there is none.
		  */
		  //Skybox* UTILS_NULLABLE GetSkybox() const noexcept;

		  /**
		   * Set the IndirectLight to use when rendering the Scene.
		   *
		   * Currently, a Scene may only have a single IndirectLight. This call replaces the current
		   * IndirectLight.
		   *
		   * @param ibl The IndirectLight to use when rendering the Scene or nullptr to unset.
		   * @see getIndirectLight
		   */
		   //void SetIndirectLight(IndirectLight* UTILS_NULLABLE ibl) noexcept;

		   /**
			* Get the IndirectLight or nullptr if none is set.
			*
			* @return the the IndirectLight or nullptr if none is set
			* @see setIndirectLight
			*/
			//IndirectLight* UTILS_NULLABLE GetIndirectLight() const noexcept;

			/**
			 * Adds an Entity to the Scene.
			 *
			 * @param entity The entity is ignored if it doesn't have a Renderable or Light component.
			 *
			 * \attention
			 *  A given Entity object can only be added once to a Scene.
			 *
			 */
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
	};

	class Archive; 
	namespace ecs { struct EntitySerializer; }

	struct CORE_EXPORT NameComponent
	{
		std::string name;

		inline void operator=(const std::string& str) { name = str; }
		inline void operator=(std::string&& str) { name = std::move(str); }
		inline bool operator==(const std::string& str) const { return name.compare(str) == 0; }

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT TransformComponent
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

	public:

		void SetDirty(const bool isDirty) { isDirty_ = isDirty; } // call when changing entity's hierarchy

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

		void SetPosition(const XMFLOAT3& p) { isDirty_ = true; position_ = p; }
		void SetScale(const XMFLOAT3& s) { isDirty_ = true; scale_ = s; }
		void SetEulerAngleZXY(const XMFLOAT3& rotAngles); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetEulerAngleZXYInDegree(const XMFLOAT3& rotAngles); // ROLL->PITCH->YAW (mainly used CG-convention) 
		void SetQuaternion(const XMFLOAT4& q) { isDirty_ = true; rotation_ = q; }
		void SetMatrix(XMFLOAT4X4 local);

		void UpdateMatrix();

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT HierarchyComponent
	{
		Entity parentEntity = INVALID_ENTITY;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// resources

	struct CORE_EXPORT MaterialComponent
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

	struct CORE_EXPORT GeometryComponent
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

	struct CORE_EXPORT TextureComponent
	{
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// scene 
	struct CORE_EXPORT RenderableComponent
	{
	private:
		bool isValid_ = false;
		Entity geometryEntity_ = INVALID_ENTITY;
		std::vector<Entity> materialEntities_;
	public:
		bool IsValid() { return isValid_; }
		void SetGeometry(const Entity geometryEntity);
		bool SetMaterial(const Entity materialEntity, const size_t slot);
		void SetMaterials(const std::vector<Entity>& materials);

		Entity GetGeometry() { return geometryEntity_; }
		Entity GetMaterial(const size_t slot) { return slot >= materialEntities_.size() ? INVALID_ENTITY : materialEntities_[slot]; }
		std::vector<Entity> GetMaterials() { return materialEntities_; }
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT LightComponent
	{
		enums::LightFlags flags = enums::LightFlags::EMPTY;

		enums::LightType type = enums::LightType::POINT;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT CameraComponent
	{
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};
}

// component factory
namespace vz::compfactory
{
	NameComponent* GetNameComponent(Entity entity);
	TransformComponent* GetTransformComponent(Entity entity);
	HierarchyComponent* GetHierarchyComponent(Entity entity);
	MaterialComponent* GetMaterialComponent(Entity entity);
	GeometryComponent* GetGeometryComponent(Entity entity);

	bool ContainNameComponent(Entity entity);
	bool ContainTransformComponent(Entity entity);
	bool ContainHierarchyComponent(Entity entity);
	bool ContainMaterialComponent(Entity entity);
	bool ContainGeometryComponent(Entity entity);
}
