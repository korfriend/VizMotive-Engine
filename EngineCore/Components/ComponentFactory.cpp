#include "Components.h"
#include "Utils/ECS.h"
#include "Common/Archive.h"
#include "Common/ResourceManager.h"

// component factory
namespace vz::compfactory
{
	using namespace vz::ecs;

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

	ComponentBase* GetComponentByVUID(const VUID vuid)
	{
		ComponentType comp_type = static_cast<ComponentType>(ComponentLibrary::GetComponentTypeByVUID(vuid));
		switch (comp_type)
		{
		case ComponentType::UNDEFINED: return nullptr;
		case ComponentType::NAME: return nameManager.GetComponent(vuid);
		case ComponentType::TRANSFORM: return transformManager.GetComponent(vuid);
		case ComponentType::HIERARCHY: return hierarchyManager.GetComponent(vuid);
		case ComponentType::RENDERABLE: return renderableManager.GetComponent(vuid);
		case ComponentType::MATERIAL: return materialManager.GetComponent(vuid);
		case ComponentType::GEOMETRY: return geometryManager.GetComponent(vuid);
		case ComponentType::TEXTURE: return textureManager.GetComponent(vuid);
		case ComponentType::LIGHT: return lightManager.GetComponent(vuid);
		case ComponentType::CAMERA: return cameraManager.GetComponent(vuid);
		default: assert(0);
		}
		return nullptr;
	}
	Entity GetEntityByVUID(const VUID vuid)
	{
		ComponentBase* comp = GetComponentByVUID(vuid);
		if (comp == nullptr)
			return INVALID_ENTITY;
		return comp->GetEntity();
	}

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
