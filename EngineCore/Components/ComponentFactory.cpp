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

	// ----- Graphics-related components -----
	ComponentManager<GRenderableComponent>& renderableManager = componentLibrary.Register<GRenderableComponent>("RENDERABLE");
	ComponentManager<GLightComponent>& lightManager = componentLibrary.Register<GLightComponent>("LIGHT");
	ComponentManager<GCameraComponent>& cameraManager = componentLibrary.Register<GCameraComponent>("CAMERA");

	ComponentManager<GMaterialComponent>& materialManager = componentLibrary.Register<GMaterialComponent>("MATERIAL");
	ComponentManager<GGeometryComponent>& geometryManager = componentLibrary.Register<GGeometryComponent>("GEOMETRY");
	ComponentManager<GTextureComponent>& textureManager = componentLibrary.Register<GTextureComponent>("TEXTURE");

	ComponentBase* GetComponentByVUID(const VUID vuid)
	{
		ComponentType comp_type = static_cast<ComponentType>(vuid & 0xFF); // using magic bits
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
	size_t SetSceneComponentsDirty(const Entity entity)
	{
		size_t num_changed = 0;
		RenderableComponent* renderable = renderableManager.GetComponent(entity);
		if (renderable) {
			renderable->SetDirty();
			num_changed++;
		}
		LightComponent* lights = lightManager.GetComponent(entity);
		if (lights) {
			lights->SetDirty();
			num_changed++;
		}
		CameraComponent* camera = cameraManager.GetComponent(entity);
		if (camera) {
			camera->SetDirty();
			num_changed++;
		}
		return num_changed;
	}

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
		HierarchyComponent* comp_parent = compfactory::GetHierarchyComponent(parent);
		if (comp_parent)
		{
			comp->SetParent(comp_parent->GetVUID());
		}
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
	TextureComponent* CreateTextureComponent(const Entity entity)
	{
		TextureComponent* comp = &textureManager.Create(entity);
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
	RenderableComponent* CreateRenderableComponent(const Entity entity)
	{
		RenderableComponent* comp = &renderableManager.Create(entity);
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
	TextureComponent* GetTextureComponent(const Entity entity)
	{
		return textureManager.GetComponent(entity);
	}
	RenderableComponent* GetRenderableComponent(const Entity entity)
	{
		return renderableManager.GetComponent(entity);
	}
	LightComponent* GetLightComponent(const Entity entity)
	{
		return lightManager.GetComponent(entity);
	}
	CameraComponent* GetCameraComponent(const Entity entity)
	{
		return cameraManager.GetComponent(entity);
	}

#define GET_COMPONENTS(T, TM) comps.clear(); size_t n = entities.size(); comps.reserve(n); \
	for (size_t i = 0; i < n; ++i) { T* comp = TM.GetComponent(entities[i]); assert(comp); comps.push_back(comp); } \
	return comps.size();

	size_t GetTransformComponents(const std::vector<Entity>& entities, std::vector<TransformComponent*>& comps)
	{
		GET_COMPONENTS(TransformComponent, transformManager);
	}
	size_t GetHierarchyComponents(const std::vector<Entity>& entities, std::vector<HierarchyComponent*>& comps)
	{
		GET_COMPONENTS(HierarchyComponent, hierarchyManager);
	}
	size_t GetMaterialComponents(const std::vector<Entity>& entities, std::vector<MaterialComponent*>& comps)
	{
		GET_COMPONENTS(MaterialComponent, materialManager);
	}
	size_t GetLightComponents(const std::vector<Entity>& entities, std::vector<LightComponent*>& comps)
	{
		GET_COMPONENTS(LightComponent, lightManager);
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
	bool ContainRenderableComponent(const Entity entity)
	{
		return renderableManager.Contains(entity);
	}
	bool ContainLightComponent(const Entity entity)
	{
		return lightManager.Contains(entity);
	}
	bool ContainCameraComponent(const Entity entity)
	{
		return cameraManager.Contains(entity);
	}

	size_t GetComponents(const Entity entity, std::vector<ComponentBase*>& components)
	{
		components.clear();
#define GET_COMP_BY_ENTITY(CM) { ComponentBase* comp = CM.GetComponent(entity); if (comp) components.push_back(comp); }

		GET_COMP_BY_ENTITY(nameManager);
		GET_COMP_BY_ENTITY(transformManager);
		GET_COMP_BY_ENTITY(hierarchyManager);
		GET_COMP_BY_ENTITY(renderableManager);
		GET_COMP_BY_ENTITY(lightManager);
		GET_COMP_BY_ENTITY(cameraManager);
		GET_COMP_BY_ENTITY(materialManager);
		GET_COMP_BY_ENTITY(geometryManager);
		GET_COMP_BY_ENTITY(textureManager);
		
		return components.size();
	}
	size_t GetEntitiesByName(const std::string& name, std::vector<Entity>& entities)
	{
		const std::vector<NameComponent>& names = nameManager.GetComponentArray();
		entities.clear();
		for (auto& it : names)
		{
			if (it.name == name)
			{
				entities.push_back(it.GetEntity());
			}
		}
		return entities.size();
	}
}
