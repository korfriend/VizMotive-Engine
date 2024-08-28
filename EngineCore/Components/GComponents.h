#include "Components.h"

using namespace vz;
namespace vz::component
{
	// ECS components

	// resources

	struct GMaterialComponent : MaterialComponent
	{

		// to do //
	};

	struct GGeometryComponent : GeometryComponent
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

	struct GTextureComponent : TextureComponent
	{

	};

	// scene 

	struct GRenderableComponent : RenderableComponent
	{
		// internal geometry
		// internal mi
		// internal texture
	};
}
