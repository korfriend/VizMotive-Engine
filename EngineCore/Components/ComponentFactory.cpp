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

	// component factory interfaces //
	NameComponent* CreateNameComponent(const Entity entity, const std::string& name)
	{
		NameComponent* comp = &nameManager.Create(entity);
		comp->name = name;
		return comp;
	}
	TransformComponent* CreateTransformComponent(const Entity entity)
	{
		TransformComponent* comp = &transformManager.Create(entity);
		return comp;
	}
	HierarchyComponent* CreateHierarchyComponent(const Entity entity, const Entity parent)
	{
		HierarchyComponent* comp = &hierarchyManager.Create(entity);
		comp->parentEntity = parent;
		return comp;
	}
	MaterialComponent* CreateMaterialComponent(const Entity entity)
	{
		MaterialComponent* comp = &materialManager.Create(entity);
		return comp;
	}
	GeometryComponent* CreateGeometryComponent(const Entity entity)
	{
		GeometryComponent* comp = &geometryManager.Create(entity);
		return comp;
	}
	LightComponent* CreateLightComponent(const Entity entity)
	{
		LightComponent* comp = &lightManager.Create(entity);
		return comp;
	}
	CameraComponent* CreateCameraComponent(const Entity entity)
	{
		CameraComponent* comp = &cameraManager.Create(entity);
		return comp;
	}

	NameComponent* GetNameComponent(const Entity entity)
	{
		return nameManager.GetComponent(entity);
	}
	TransformComponent* GetTransformComponent(const Entity entity)
	{
		return transformManager.GetComponent(entity);
	}
	HierarchyComponent* GetHierarchyComponent(const Entity entity)
	{
		return hierarchyManager.GetComponent(entity);
	}
	MaterialComponent* GetMaterialComponent(const Entity entity)
	{
		return materialManager.GetComponent(entity);
	}
	GeometryComponent* GetGeometryComponent(const Entity entity)
	{
		return geometryManager.GetComponent(entity);
	}
	LightComponent* GetLightComponent(const Entity entity)
	{
		return lightManager.GetComponent(entity);
	}
	CameraComponent* GetCameraComponent(const Entity entity)
	{
		return cameraManager.GetComponent(entity);
	}

	bool ContainNameComponent(const Entity entity)
	{
		return nameManager.Contains(entity);
	}
	bool ContainTransformComponent(const Entity entity)
	{
		return transformManager.Contains(entity);
	}
	bool ContainHierarchyComponent(const Entity entity)
	{
		return hierarchyManager.Contains(entity);
	}
	bool ContainMaterialComponent(const Entity entity)
	{
		return materialManager.Contains(entity);
	}
	bool ContainGeometryComponent(const Entity entity)
	{
		return geometryManager.Contains(entity);
	}
	bool ContainLightComponent(const Entity entity)
	{
		return lightManager.Contains(entity);
	}
	bool ContainCameraComponent(const Entity entity)
	{
		return cameraManager.Contains(entity);
	}
}
