#pragma once
#include "../Common/CommonInclude.h"
#include "../Common/Archive.h"
#include "../Utils/ECS.h"

#include "../VzEnums.h"

#include <string>

using namespace vz;
namespace vz::component
{
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
		bool isMatrixAutoUpdate = true;
		XMFLOAT3 scale = XMFLOAT3(1, 1, 1);
		XMFLOAT4 rotation = XMFLOAT4(0, 0, 0, 1);	// this is a quaternion
		XMFLOAT3 position = XMFLOAT3(0, 0, 0);
		XMFLOAT4X4 local = vz::math::IDENTITY_MATRIX;

		// Non-serialized attributes:

		// The world matrix can be computed from local scale, rotation, translation
		//	- by calling UpdateTransform()
		//	- or by calling SetDirty() and letting the TransformUpdateSystem handle the updating
		XMFLOAT4X4 world = vz::math::IDENTITY_MATRIX;

		XMFLOAT3 GetPosition() const;
		XMFLOAT4 GetRotation() const;
		XMFLOAT3 GetScale() const;
		XMFLOAT3 GetForward() const;
		XMFLOAT3 GetUp() const;
		XMFLOAT3 GetRight() const;

		void SetPosition();
		void SetScale(const XMFLOAT3 scale);
		void SetRotation(const XMFLOAT3 rotAngles, const enums::EulerAngle order = enums::EulerAngle::XYZ);
		void SetQuaternion(const XMFLOAT4 q, const enums::EulerAngle order = enums::EulerAngle::XYZ);

		void UpdateMatrix();

		// Apply the world matrix to the local space. This overwrites scale, rotation, position
		void ApplyTransform();

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	struct HierarchyComponent
	{
		vz::ecs::Entity parentID = vz::ecs::INVALID_ENTITY;

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
	};

	// resources

	struct MaterialComponent
	{
		

		void Serialize(vz::Archive& archive, vz::ecs::EntitySerializer& seri);
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
	};

	struct TextureComponent
	{

	};

	// scene 

	struct RenderableComponent
	{
		ecs::Entity geometryEntity = ecs::INVALID_ENTITY;
		std::vector<ecs::Entity> miEntities;

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
