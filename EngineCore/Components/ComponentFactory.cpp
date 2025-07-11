#include "Common/Engine_Internal.h"
#include "Common/Archive.h"
#include "Common/ResourceManager.h"
#include "Utils/ECS.h"
#include "Utils/Backlog.h"

#include <unordered_set>

// component factory
namespace vz::compfactory
{
	using namespace vz::ecs;

	ComponentLibrary componentLibrary;

	std::unordered_map<std::string, std::unordered_set<Entity>> lookupName2Entities;

	ComponentManager<NameComponent>& nameManager = componentLibrary.Register<NameComponent>("NAME");
	ComponentManager<TransformComponent>& transformManager = componentLibrary.Register<TransformComponent>("TANSFORM");
	ComponentManager<HierarchyComponent>& hierarchyManager = componentLibrary.Register<HierarchyComponent>("HIERARCHY");
	ComponentManager<LayeredMaskComponent>& layeredmaskManager = componentLibrary.Register<LayeredMaskComponent>("LAYERDMASK");
	ComponentManager<ColliderComponent>& colliderManager = componentLibrary.Register<ColliderComponent>("COLLIDER");

	// ----- Graphics-related components -----
	ComponentManager<GRenderableComponent>& renderableManager = componentLibrary.Register<GRenderableComponent>("RENDERABLE");
	ComponentManager<GSpriteComponent>& spriteManager = componentLibrary.Register<GSpriteComponent>("SPRITE");
	ComponentManager<GSpriteFontComponent>& spritefontManager = componentLibrary.Register<GSpriteFontComponent>("SPRITEFONT");
	ComponentManager<GLightComponent>& lightManager = componentLibrary.Register<GLightComponent>("LIGHT");
	ComponentManager<GCameraComponent>& cameraManager = componentLibrary.Register<GCameraComponent>("CAMERA");
	ComponentManager<GSlicerComponent>& slicerManager = componentLibrary.Register<GSlicerComponent>("SLICER");

	ComponentManager<GMaterialComponent>& materialManager = componentLibrary.Register<GMaterialComponent>("MATERIAL");
	ComponentManager<GGeometryComponent>& geometryManager = componentLibrary.Register<GGeometryComponent>("GEOMETRY");
	ComponentManager<GTextureComponent>& textureManager = componentLibrary.Register<GTextureComponent>("TEXTURE");
	ComponentManager<GVolumeComponent>& volumeManager = componentLibrary.Register<GVolumeComponent>("VOLUMETEXTURE");

	ComponentBase* GetComponentByVUID(const VUID vuid)
	{
		ComponentType comp_type = static_cast<ComponentType>(uint32_t(vuid & 0xFF)); // using magic bits
		switch (comp_type)
		{
		case ComponentType::UNDEFINED: return nullptr;
		case ComponentType::NAME: return nameManager.GetComponentByVUID(vuid);
		case ComponentType::TRANSFORM: return transformManager.GetComponentByVUID(vuid);
		case ComponentType::HIERARCHY: return hierarchyManager.GetComponentByVUID(vuid);
		case ComponentType::LAYERDMASK: return layeredmaskManager.GetComponentByVUID(vuid);
		case ComponentType::COLLIDER: return colliderManager.GetComponentByVUID(vuid);
		case ComponentType::RENDERABLE: return renderableManager.GetComponentByVUID(vuid);
		case ComponentType::SPRITE: return spriteManager.GetComponentByVUID(vuid);
		case ComponentType::SPRITEFONT: return spritefontManager.GetComponentByVUID(vuid);
		case ComponentType::MATERIAL: return materialManager.GetComponentByVUID(vuid);
		case ComponentType::GEOMETRY: return geometryManager.GetComponentByVUID(vuid);
		case ComponentType::TEXTURE: return textureManager.GetComponentByVUID(vuid);
		case ComponentType::VOLUMETEXTURE: return volumeManager.GetComponentByVUID(vuid);
		case ComponentType::LIGHT: return lightManager.GetComponentByVUID(vuid);
		case ComponentType::CAMERA: return cameraManager.GetComponentByVUID(vuid);
		case ComponentType::SLICER: return slicerManager.GetComponentByVUID(vuid);
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

#define ENTITY_UPDATE(ENTITY) Entity ENTITY = entity; if (entity == 0u) ENTITY = ecs::CreateEntity(); std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
	NameComponent* CreateNameComponent(const Entity entity, const std::string& name)
	{
		ENTITY_UPDATE(entity_update);
		NameComponent* comp = &nameManager.Create(entity_update);
		comp->SetName(name);
		return comp;
	}
	TransformComponent* CreateTransformComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		TransformComponent* comp = &transformManager.Create(entity_update);
		return comp;
	}
	HierarchyComponent* CreateHierarchyComponent(const Entity entity, const Entity parent)
	{
		ENTITY_UPDATE(entity_update);
		HierarchyComponent* comp = &hierarchyManager.Create(entity_update);
		HierarchyComponent* comp_parent = hierarchyManager.GetComponent(parent);
		if (comp_parent)
		{
			comp->SetParentByVUID(comp_parent->GetVUID());
		}
		return comp;
	}
	LayeredMaskComponent* CreateLayeredMaskComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		LayeredMaskComponent* comp = &layeredmaskManager.Create(entity_update);
		return comp;
	}
	MaterialComponent* CreateMaterialComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		MaterialComponent* comp = &materialManager.Create(entity_update);
		return comp;
	}
	GeometryComponent* CreateGeometryComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		GeometryComponent* comp = &geometryManager.Create(entity_update);
		return comp;
	}
	TextureComponent* CreateTextureComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		TextureComponent* comp = &textureManager.Create(entity_update);
		return comp;
	}
	VolumeComponent* CreateVolumeComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		VolumeComponent* comp = &volumeManager.Create(entity_update);
		return comp;
	}
	ColliderComponent* CreateColliderComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		ColliderComponent* comp = &colliderManager.Create(entity_update);
		return comp;
	}
	LightComponent* CreateLightComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		LightComponent* comp = &lightManager.Create(entity_update);
		return comp;
	}
	CameraComponent* CreateCameraComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		CameraComponent* comp = &cameraManager.Create(entity_update);
		return comp;
	}
	SlicerComponent* CreateSlicerComponent(const Entity entity, const bool curvedSlicer)
	{
		ENTITY_UPDATE(entity_update);
		SlicerComponent* comp = &slicerManager.Create(entity_update, curvedSlicer);
		return comp;
	}
	RenderableComponent* CreateRenderableComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		RenderableComponent* comp = &renderableManager.Create(entity_update);
		return comp;
	}
	SpriteComponent* CreateSpriteComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		SpriteComponent* comp = &spriteManager.Create(entity_update);
		return comp;
	}
	SpriteFontComponent* CreateSpriteFontComponent(const Entity entity)
	{
		ENTITY_UPDATE(entity_update);
		SpriteFontComponent* comp = &spritefontManager.Create(entity_update);
		return comp;
	}

#define RETURN_GET_COMP(COMP_TYPE, COMP_MNG, ENTITY) COMP_TYPE* comp = COMP_MNG.GetComponent(entity); return comp;
	NameComponent* GetNameComponent(const Entity entity)
	{
		RETURN_GET_COMP(NameComponent, nameManager, entity);
	}
	TransformComponent* GetTransformComponent(const Entity entity)
	{
		RETURN_GET_COMP(TransformComponent, transformManager, entity);
	}
	HierarchyComponent* GetHierarchyComponent(const Entity entity)
	{
		RETURN_GET_COMP(HierarchyComponent, hierarchyManager, entity);
	}
	LayeredMaskComponent* GetLayeredMaskComponent(const Entity entity)
	{
		RETURN_GET_COMP(LayeredMaskComponent, layeredmaskManager, entity);
	}
	MaterialComponent* GetMaterialComponent(const Entity entity)
	{
		RETURN_GET_COMP(MaterialComponent, materialManager, entity);
	}
	GeometryComponent* GetGeometryComponent(const Entity entity)
	{
		RETURN_GET_COMP(GeometryComponent, geometryManager, entity);
	}
	TextureComponent* GetTextureComponent(const Entity entity)
	{
		TextureComponent* comp = textureManager.GetComponent(entity);
		if (comp == nullptr)
		{
			comp = volumeManager.GetComponent(entity);
		}
		return comp;
	}
	VolumeComponent* GetVolumeComponent(const Entity entity)
	{
		RETURN_GET_COMP(VolumeComponent, volumeManager, entity);
	}
	ColliderComponent* GetColliderComponent(const Entity entity)
	{
		RETURN_GET_COMP(ColliderComponent, colliderManager, entity);
	}
	RenderableComponent* GetRenderableComponent(const Entity entity)
	{
		RETURN_GET_COMP(RenderableComponent, renderableManager, entity);
	}
	SpriteComponent* GetSpriteComponent(const Entity entity)
	{
		RETURN_GET_COMP(SpriteComponent, spriteManager, entity);
	}
	SpriteFontComponent* GetSpriteFontComponent(const Entity entity)
	{
		RETURN_GET_COMP(SpriteFontComponent, spritefontManager, entity);
	}
	LightComponent* GetLightComponent(const Entity entity)
	{
		RETURN_GET_COMP(LightComponent, lightManager, entity);
	}
	CameraComponent* GetCameraComponent(const Entity entity)
	{
		CameraComponent* comp = cameraManager.GetComponent(entity);
		if (comp == nullptr)
		{
			comp = slicerManager.GetComponent(entity);
		}
		return comp;
	}
	SlicerComponent* GetSlicerComponent(const Entity entity)
	{
		RETURN_GET_COMP(SlicerComponent, slicerManager, entity);
	}

	NameComponent* GetNameComponentByVUID(const VUID vuid)
	{
		return GetNameComponent(GetEntityByVUID(vuid));
	}
	TransformComponent* GetTransformComponentByVUID(const VUID vuid)
	{
		return GetTransformComponent(GetEntityByVUID(vuid));
	}
	HierarchyComponent* GetHierarchyComponentByVUID(const VUID vuid)
	{
		return GetHierarchyComponent(GetEntityByVUID(vuid));
	}
	ColliderComponent* GetColliderComponentByVUID(const VUID vuid)
	{
		return GetColliderComponent(GetEntityByVUID(vuid));
	}
	MaterialComponent* GetMaterialComponentByVUID(const VUID vuid)
	{
		return GetMaterialComponent(GetEntityByVUID(vuid));
	}
	GeometryComponent* GetGeometryComponentByVUID(const VUID vuid)
	{
		return GetGeometryComponent(GetEntityByVUID(vuid));
	}
	TextureComponent* GetTextureComponentByVUID(const VUID vuid)
	{
		return GetTextureComponent(GetEntityByVUID(vuid));
	}
	VolumeComponent* GetVolumeComponentByVUID(const VUID vuid)
	{
		return GetVolumeComponent(GetEntityByVUID(vuid));
	}
	RenderableComponent* GetRenderableComponentByVUID(const VUID vuid)
	{
		return GetRenderableComponent(GetEntityByVUID(vuid));
	}
	LightComponent* GetLightComponentByVUID(const VUID vuid)
	{
		return GetLightComponent(GetEntityByVUID(vuid));
	}
	CameraComponent* GetCameraComponentByVUID(const VUID vuid)
	{
		return GetCameraComponent(GetEntityByVUID(vuid));
	}
	SlicerComponent* GetSlicerComponentByVUID(const VUID vuid)
	{
		return GetSlicerComponent(GetEntityByVUID(vuid));
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
	bool ContainLayeredMaskComponent(const Entity entity)
	{
		return layeredmaskManager.Contains(entity);
	}
	bool ContainColliderComponent(const Entity entity)
	{
		return colliderManager.Contains(entity);
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
	bool ContainSpriteComponent(const Entity entity)
	{
		return spriteManager.Contains(entity);
	}
	bool ContainSpriteFontComponent(const Entity entity)
	{	
		return spritefontManager.Contains(entity);
	}
	bool ContainLightComponent(const Entity entity)
	{
		return lightManager.Contains(entity);
	}
	bool ContainCameraComponent(const Entity entity)
	{
		return cameraManager.Contains(entity);
	}
	bool ContainSlicerComponent(const Entity entity)
	{
		return slicerManager.Contains(entity);
	}
	bool ContainTextureComponent(const Entity entity)
	{
		return textureManager.Contains(entity);
	}
	bool ContainVolumeComponent(const Entity entity)
	{
		return volumeManager.Contains(entity);
	}

	size_t GetComponents(const Entity entity, std::vector<ComponentBase*>& components)
	{
		components.clear();
#define GET_COMP_BY_ENTITY(CM) { ComponentBase* comp = CM.GetComponent(entity); if (comp) components.push_back(comp); }

		GET_COMP_BY_ENTITY(nameManager);
		GET_COMP_BY_ENTITY(transformManager);
		GET_COMP_BY_ENTITY(hierarchyManager);
		GET_COMP_BY_ENTITY(layeredmaskManager);
		GET_COMP_BY_ENTITY(colliderManager);
		GET_COMP_BY_ENTITY(renderableManager);
		GET_COMP_BY_ENTITY(spriteManager);
		GET_COMP_BY_ENTITY(spritefontManager);
		GET_COMP_BY_ENTITY(lightManager);
		GET_COMP_BY_ENTITY(cameraManager);
		GET_COMP_BY_ENTITY(materialManager);
		GET_COMP_BY_ENTITY(geometryManager);
		GET_COMP_BY_ENTITY(textureManager);
		GET_COMP_BY_ENTITY(volumeManager);

		return components.size();
	}
	size_t GetEntitiesByName(const std::string& name, std::vector<Entity>& entities)
	{
		entities.clear();

		auto it = lookupName2Entities.find(name);
		if (it == lookupName2Entities.end())
		{
			return 0;
		}
		auto entity_set = it->second;
		entities.resize(entity_set.size());
		std::copy(entity_set.begin(), entity_set.end(), entities.begin());

		//const std::vector<NameComponent>& names = nameManager.GetComponentArray();
		//for (auto& it : names)
		//{
		//	if (it.name == name)
		//	{
		//		entities.push_back(it.GetEntity());
		//	}
		//}

		return entities.size();
	}
	Entity GetFirstEntityByName(const std::string& name)
	{
		auto it = lookupName2Entities.find(name);
		if (it == lookupName2Entities.end())
		{
			return INVALID_ENTITY;
		}
		return *it->second.begin();
	}

	void EntitySafeExecute(const std::function<void(const std::vector<Entity>&)>& task, const std::vector<Entity>& entities)
	{
		std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
		task(entities);
	}
}
	
namespace vz::compfactory
{
	size_t Destroy(const Entity entity)
	{
		std::unordered_set<VUID> vuids;
		size_t num_destroyed = 0u;
		for (auto& entry : componentLibrary.entries)
		{
			auto& comp_manager = entry.second.component_manager;
			auto& entities = comp_manager->GetEntityArray();
			bool is_contain = false;
			for (auto ett : entities)
			{
				if (ett == entity)
				{
					is_contain = true;
					break;
				}
			}
			if (is_contain)
			{
				if (entry.first == "NAME")
				{
					NameComponent* name_comp = nameManager.GetComponent(entity);
					assert(lookupName2Entities.count(name_comp->GetName()) > 0);
					auto& namelookup = lookupName2Entities[name_comp->GetName()];
					namelookup.erase(entity);
					if (namelookup.size() == 0)
					{
						lookupName2Entities.erase(name_comp->GetName());
					}
				}
				// dummy call
				VUID vuid = entry.second.component_manager->GetVUID(entity);
				vuids.insert(vuid);

				comp_manager->Remove(entity);

				num_destroyed++;
			}
		}
		for (auto& entry : componentLibrary.entries)
		{
			for (auto vuid : vuids)
			{
				entry.second.component_manager->ResetAllRefComponents(vuid);
			}
		}
		return num_destroyed;
	}

	size_t DestroyAll()
	{
		size_t num_destroyed = 0u;
		for (auto& entry : componentLibrary.entries)
		{
			auto& comp_manager = entry.second.component_manager;
			num_destroyed += comp_manager->GetCount();
			comp_manager->Clear();
		}
		lookupName2Entities.clear();
		return num_destroyed;
	}
}
