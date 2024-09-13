#pragma once
#include "CommonInclude.h"

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

	struct CORE_EXPORT HierarchyComponent
	{
		Entity parentEntity = INVALID_ENTITY;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// resources

	struct CORE_EXPORT MaterialComponent
	{
	public:
		enum class RenderFlags
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
		enum TEXTURESLOT
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
			VOLUMEMAP,

			TEXTURESLOT_COUNT
		};

	private:
		bool isDirty_ = true;
		uint32_t renderOptionFlags_ = (uint32_t)RenderFlags::CAST_SHADOW;
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

		Entity textures[TEXTURESLOT_COUNT] = {}; // texture component map

		int customShaderID = -1;

		// Non-serialized attributes:
		int sampler_descriptor = -1; // optional

		inline float GetOpacity() const { return baseColor.w; }
		inline float GetEmissiveStrength() const { return emissiveColor.w; }

		inline void SetDirty(bool value = true) { isDirty_ = value; }
		inline bool IsDirty() const { return isDirty_; }

		inline void SetCastShadow(bool value) { SetDirty(); if (value) { renderOptionFlags_ |= (uint32_t)RenderFlags::CAST_SHADOW; } else { renderOptionFlags_ &= ~(uint32_t)RenderFlags::CAST_SHADOW; } }
		inline void SetReceiveShadow(bool value) { SetDirty(); if (value) { renderOptionFlags_ &= ~(uint32_t)RenderFlags::DISABLE_RECEIVE_SHADOW; } else { renderOptionFlags_ |= (uint32_t)RenderFlags::DISABLE_RECEIVE_SHADOW; } }

		// Create texture resources for GPU
		void UpdateAssociatedTextures(bool force_recreate = false);

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT GeometryComponent
	{
		struct Primitive {
			std::vector<char> vertexBuffer;
			std::vector<char> indexBuffer;
			
			Aabb aabb; // object-space bounding box
			UvMap uvmap; // mapping from each glTF UV set to either UV0 or UV1 (8 bytes)
			uint32_t morphTargetOffset;
			std::vector<int> slotIndices;

			enums::PrimitiveType ptype = enums::PrimitiveType::TRIANGLES;
		};

		std::vector<Primitive> parts;

	public:
		bool isSystem = false;
		//gltfio::FilamentAsset* assetOwner = nullptr; // has ownership
		//filament::Aabb aabb;
		//void Set(const std::vector<VzPrimitive>& primitives);
		//std::vector<VzPrimitive>* Get();
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct CORE_EXPORT TextureComponent
	{
		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// scene 

	struct CORE_EXPORT RenderableComponent
	{
		Entity geometryEntity = INVALID_ENTITY;
		std::vector<Entity> miEntities;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
		// internal geometry
		// internal mi
		// internal texture
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
