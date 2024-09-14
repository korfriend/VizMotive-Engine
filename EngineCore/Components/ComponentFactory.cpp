#include "Components.h"
#include "Utils/ECS.h"
#include "Common/Archive.h"
#include "Common/ResourceManager.h"

// component factory
namespace vz::compfactory
{
	using namespace vz::ecs;
	using ENTIRY_INSTANCE = size_t;
	static const ENTIRY_INSTANCE INVALID_INSTANCE = ~0ull;

	ComponentLibrary componentLibrary;

	ComponentManager<NameComponent>& nameManager = componentLibrary.Register<NameComponent>("NAME");
	ComponentManager<TransformComponent>& transformManager = componentLibrary.Register<TransformComponent>("TANSFORM");
	ComponentManager<HierarchyComponent>& hierarchyManager = componentLibrary.Register<HierarchyComponent>("HIERARCHY");
	ComponentManager<RenderableComponent>& renderableManager = componentLibrary.Register<RenderableComponent>("RENDERABLE");
	ComponentManager<LightComponent>& lightManager = componentLibrary.Register<LightComponent>("LIGHT");
	ComponentManager<CameraComponent>& cameraManager = componentLibrary.Register<CameraComponent>("CAMERA");

	ComponentManager<GMaterialComponent>& materialManager = componentLibrary.Register<GMaterialComponent>("MATERIAL");
	ComponentManager<GGeometryComponent>& geometryManager = componentLibrary.Register<GGeometryComponent>("GEOMETRY");
	ComponentManager<GTextureComponent>& textureManager = componentLibrary.Register<GTextureComponent>("TEXTURE");

	// component helpers //

	NameComponent* GetNameComponent(Entity entity)
	{
		return nameManager.GetComponent(entity);
	}
	TransformComponent* GetTransformComponent(Entity entity)
	{
		return transformManager.GetComponent(entity);
	}
	HierarchyComponent* GetHierarchyComponent(Entity entity)
	{
		return hierarchyManager.GetComponent(entity);
	}
	MaterialComponent* GetMaterialComponent(Entity entity)
	{
		return materialManager.GetComponent(entity);
	}
	GeometryComponent* GetGeometryComponent(Entity entity)
	{
		return geometryManager.GetComponent(entity);
	}

	bool ContainNameComponent(Entity entity)
	{
		return nameManager.Contains(entity);
	}
	bool ContainTransformComponent(Entity entity)
	{
		return transformManager.Contains(entity);
	}
	bool ContainHierarchyComponent(Entity entity)
	{
		return hierarchyManager.Contains(entity);
	}
	bool ContainMaterialComponent(Entity entity)
	{
		return materialManager.Contains(entity);
	}
	bool ContainGeometryComponent(Entity entity)
	{
		return geometryManager.Contains(entity);
	}
}
