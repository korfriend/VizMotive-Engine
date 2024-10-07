#include "VzEngineAPIs.h"
#include "Components/GComponents.h"
#include "Utils/Backlog.h"
#include "Utils/Platform.h"
#include "Utils/ECS.h"
#include "Common/Backend/GBackendDevice.h"
#include "Common/RenderPath3D.h"
#include "Common/Initializer.h"
#include "Common/Backend/GRendererInterface.h"

namespace vz
{
	GraphicsPackage graphicsPackage;
}

namespace vzm
{
	using namespace vz;

#define CHECK_API_VALIDITY(RET) if (!initialized) { backlog::post("High-level API is not initialized!!", backlog::LogLevel::Error); return RET; }

	bool initialized = false;
	vz::graphics::GraphicsDevice* graphicsDevice = nullptr;

	namespace vzcomp
	{
		std::unordered_map<VID, VzBaseComp*> lookup;

		std::unordered_map<SceneVID, std::unique_ptr<VzScene>> scenes;
		
		std::unordered_map<RendererVID, std::unique_ptr<VzRenderer>> renderers;

		std::unordered_map<CamVID, std::unique_ptr<VzCamera>> cameras;
		std::unordered_map<ActorVID, std::unique_ptr<VzActor>> actors;
		std::unordered_map<LightVID, std::unique_ptr<VzLight>> lights;

		std::unordered_map<GeometryVID, std::unique_ptr<VzGeometry>> geometries;
		std::unordered_map<MaterialVID, std::unique_ptr<VzMaterial>> materials;
		std::unordered_map<TextureVID, std::unique_ptr<VzTexture>> textures;
	}

	bool InitEngineLib(const vzm::ParamMap<std::string>& arguments)
	{
		if (initialized)
		{
			backlog::post("Already initialized!", backlog::LogLevel::Warn);
			return false;
		}

		// assume DX12 rendering engine
		std::string api = arguments.GetString("API", "DX12");
		if (!graphicsPackage.Init(api))
		{
			backlog::post("Invalid Graphics API : " + api, backlog::LogLevel::Error);
			return false;
		}

		// initialize the graphics backend
		graphicsPackage.pluginInitializer();

		// graphics device
		graphicsDevice = graphicsPackage.pluginGetDev();
		graphics::GetDevice() = graphicsDevice;

		// engine core initializer
		initializer::InitializeComponentsAsync();
		//initializer::InitializeComponentsImmediate();
		
		initialized = true;
		return true;
	}

	VzScene* NewScene(const std::string& name)
	{
		CHECK_API_VALIDITY(nullptr);
		Scene* scene = Scene::CreateScene(name);
		SceneVID vid = scene->GetSceneEntity();
		auto it = vzcomp::scenes.emplace(vid, std::make_unique<VzScene>(vid, "vzm::NewScene"));
		compfactory::CreateNameComponent(vid, name);
		vzcomp::lookup[vid] = it.first->second.get();
		return it.first->second.get();
	}

	VzRenderer* NewRenderer(const std::string& name)
	{
		CHECK_API_VALIDITY(nullptr);
		RenderPath3D* renderer = canvas::CreateRenderPath3D(graphicsDevice, name);
		RendererVID vid = renderer->GetEntity();
		auto it = vzcomp::renderers.emplace(vid, std::make_unique<VzRenderer>(vid, "vzm::NewRenderer"));
		compfactory::CreateNameComponent(vid, name);
		vzcomp::lookup[vid] = it.first->second.get();
		return it.first->second.get();
	}

	VzSceneComp* newSceneComponent(const SCENE_COMPONENT_TYPE compType, const std::string& compName, const VID parentVid)
	{
		CHECK_API_VALIDITY(nullptr);
		if (compType == SCENE_COMPONENT_TYPE::SCENEBASE)
		{
			return nullptr;
		}

		Entity entity = ecs::CreateEntity();

		compfactory::CreateNameComponent(entity, compName);
		compfactory::CreateTransformComponent(entity);
		compfactory::CreateHierarchyComponent(entity);

		VID vid = entity;
		VzSceneComp* hlcomp = nullptr;

		switch (compType)
		{
		case SCENE_COMPONENT_TYPE::ACTOR:
			compfactory::CreateRenderableComponent(entity);
			{
				auto it = vzcomp::actors.emplace(vid, std::make_unique<VzActor>(vid, "vzm::NewSceneComponent"));
				hlcomp = (VzSceneComp*)it.first->second.get();
			}
			break;
		case SCENE_COMPONENT_TYPE::LIGHT:
			compfactory::CreateLightComponent(entity);
			{
				auto it = vzcomp::lights.emplace(vid, std::make_unique<VzLight>(vid, "vzm::NewSceneComponent"));
				hlcomp = (VzSceneComp*)it.first->second.get();
			}
			break;
		case SCENE_COMPONENT_TYPE::CAMERA:
			compfactory::CreateCameraComponent(entity);
			{
				auto it = vzcomp::cameras.emplace(vid, std::make_unique<VzCamera>(vid, "vzm::NewSceneComponent"));
				hlcomp = (VzSceneComp*)it.first->second.get();
			}
			break;
		default:
			backlog::post("vzm::NewSceneComponent >> Invalid SCENE_COMPONENT_TYPE", backlog::LogLevel::Error);
			return nullptr;
		}

		if (parentVid != INVALID_VID)
		{
			AppendSceneCompVidTo(vid, parentVid);
		}

		vzcomp::lookup[vid] = hlcomp;
		return hlcomp;
	}

	VzCamera* NewCamera(const std::string& name, const VID parentVid)
	{
		return (VzCamera*)newSceneComponent(SCENE_COMPONENT_TYPE::CAMERA, name, parentVid);
	}
	VzActor* NewActor(const std::string& name, const GeometryVID vidGeo, const MaterialVID vidMat, const VID parentVid)
	{
		VzActor* actor = (VzActor*)newSceneComponent(SCENE_COMPONENT_TYPE::ACTOR, name, parentVid);
		if (vidGeo) actor->SetGeometry(vidGeo);
		if (vidMat) actor->SetMaterial(vidMat);
		return (VzActor*)newSceneComponent(SCENE_COMPONENT_TYPE::ACTOR, name, parentVid);
	}
	VzActor* NewActor(const std::string& name, const VzGeometry* geometry, const VzMaterial* material, const VID parentVid)
	{
		return NewActor(name, geometry->GetVID(), material->GetVID(), parentVid);
	}
	VzLight* NewLight(const std::string& name, const VID parentVid)
	{
		return (VzLight*)newSceneComponent(SCENE_COMPONENT_TYPE::LIGHT, name, parentVid);
	}

	VzResource* newResComponent(const RES_COMPONENT_TYPE compType, const std::string& compName)
	{
		CHECK_API_VALIDITY(nullptr);
		if (compType == RES_COMPONENT_TYPE::RESOURCE)
		{
			return nullptr;
		}

		Entity entity = ecs::CreateEntity();

		compfactory::CreateNameComponent(entity, compName);

		VID vid = entity;
		VzResource* hlcomp = nullptr;

		switch (compType)
		{
		case RES_COMPONENT_TYPE::GEOMETRY:
			compfactory::CreateGeometryComponent(entity);
			{
				auto it = vzcomp::geometries.emplace(vid, std::make_unique<VzGeometry>(vid, "vzm::NewResComponent"));
				hlcomp = (VzGeometry*)it.first->second.get();
			}
			break;
		case RES_COMPONENT_TYPE::MATERIAL:
			compfactory::CreateMaterialComponent(entity);
			{
				auto it = vzcomp::materials.emplace(vid, std::make_unique<VzMaterial>(vid, "vzm::NewResComponent"));
				hlcomp = (VzMaterial*)it.first->second.get();
			}
			break;
		case RES_COMPONENT_TYPE::TEXTURE:
			compfactory::CreateTextureComponent(entity);
			{
				auto it = vzcomp::textures.emplace(vid, std::make_unique<VzTexture>(vid, "vzm::NewResComponent"));
				hlcomp = (VzTexture*)it.first->second.get();
			}
			break;
		default:
			backlog::post("vzm::NewResComponent >> Invalid RES_COMPONENT_TYPE", backlog::LogLevel::Error);
			return nullptr;
		}
		vzcomp::lookup[vid] = hlcomp;
		return hlcomp;
	}

	VzGeometry* NewGeometry(const std::string& name)
	{
		return (VzGeometry*)newResComponent(RES_COMPONENT_TYPE::GEOMETRY, name);
	}
	VzMaterial* NewMaterial(const std::string& name)
	{
		return (VzMaterial*)newResComponent(RES_COMPONENT_TYPE::MATERIAL, name);
	}
	VzTexture* NewTexture(const std::string& name)
	{
		return (VzTexture*)newResComponent(RES_COMPONENT_TYPE::TEXTURE, name);
	}

	void getDescendants(const Entity ett, std::vector<Entity>& decendants)
	{
		HierarchyComponent* hier = compfactory::GetHierarchyComponent(ett);
		for (auto it : hier->GetChildren())
		{
			Entity ett = compfactory::GetEntityByVUID(it);
			decendants.push_back(ett);
			getDescendants(ett, decendants);
		}
	};

	bool appendSceneEntityToParent(const VID vidSrc, const VID vidDst)
	{
		assert(vidSrc != vidDst);

		auto getSceneAndVid = [](Scene** scene, const VID vid)
			{
				SceneVID vid_scene = vid;
				*scene = Scene::GetScene(vid_scene);
				if (*scene == nullptr)
				{
					auto itr = vzcomp::actors.find(vid);
					auto itl = vzcomp::lights.find(vid);
					auto itc = vzcomp::cameras.find(vid);
					if (itr == vzcomp::actors.end()
						&& itl == vzcomp::lights.end()
						&& itc == vzcomp::cameras.end())
					{
						vid_scene = INVALID_VID;
					}
					else
					{
						vid_scene = std::max(std::max(itl != vzcomp::lights.end() ? itl->second.get()->sceneVid : INVALID_VID,
							itr != vzcomp::actors.end() ? itr->second.get()->sceneVid : INVALID_VID),
							itc != vzcomp::cameras.end() ? itc->second.get()->sceneVid : INVALID_VID);
						//assert(vid_scene != INVALID_VID); can be INVALID_VID
						*scene = Scene::GetScene(vid_scene);
					}
				}
				return vid_scene;
			};

		Scene* scene_src = nullptr;
		Scene* scene_dst = nullptr;
		SceneVID vid_scene_src = getSceneAndVid(&scene_src, vidSrc);
		SceneVID vid_scene_dst = getSceneAndVid(&scene_dst, vidDst);

		Entity ett_src = vidSrc;
		Entity ett_dst = vidDst;

		// case 1. both entities are actor
		// case 2. src is scene and dst is actor
		// case 3. src is actor and dst is scene
		// case 4. both entities are scenes
		// note that actor entity must have transform component!
		std::vector<Entity> entities_moving;
		if (vidSrc != vid_scene_src && vidDst != vid_scene_dst)
		{
			// case 1. both entities are actor
			HierarchyComponent* hier_src = compfactory::GetHierarchyComponent(ett_src);
			HierarchyComponent* hier_dst = compfactory::GetHierarchyComponent(ett_dst);
			assert(hier_src != nullptr && hier_dst != nullptr);

			hier_src->SetParent(hier_dst->GetVUID());

			entities_moving.push_back(ett_src);
			getDescendants(ett_src, entities_moving);
		}
		else if (vidSrc == vid_scene_src && vidDst != vid_scene_dst)
		{
			assert(scene_src != scene_dst && "scene cannot be appended to its component");

			// case 2. src is scene and dst is actor
			HierarchyComponent* hier_dst = compfactory::GetHierarchyComponent(ett_dst);
			assert(hier_dst != nullptr && "vidDst is invalid");

			std::vector<Entity> scene_entities;
			scene_src->GetEntities(scene_entities);
			for (Entity ett : scene_entities)
			{
				entities_moving.push_back(ett);

				HierarchyComponent* hier = compfactory::GetHierarchyComponent(ett);
				if (hier->GetParent() == 0u)
				{
					hier->SetParent(hier_dst->GetVUID());
				}
			}
			Scene::DestoryScene(vid_scene_src);
			scene_src = nullptr;
		}
		else if (vidSrc != vid_scene_src && vidDst == vid_scene_dst)
		{
			// case 3. src is actor and dst is scene or zero
			// scene_src == scene_dst means that 
			//    vidSrc is appended to its root

			HierarchyComponent* hier_src = compfactory::GetHierarchyComponent(ett_src);
			assert(hier_src != nullptr && "vidSrc is invalid");
			hier_src->SetParent(0u);

			entities_moving.push_back(ett_src);
			getDescendants(ett_src, entities_moving);
		}
		else
		{
			assert(vidSrc == vid_scene_src && vidDst == vid_scene_dst);
			assert(scene_src != scene_dst);
			if (scene_src == nullptr)
			{
				return false;
			}

			std::vector<Entity> scene_entities;
			scene_src->GetEntities(scene_entities);
			// case 4. both entities are scenes
			for (Entity ett : scene_entities) 
			{
				entities_moving.push_back(ett);
			}

			Scene::DestoryScene(vid_scene_src);
			scene_src = nullptr;
		}

		// NOTE 
		// a scene can have entities with components that are not renderable
		// they are supposed to be ignored during the rendering pipeline

		for (auto& it : entities_moving)
		{
			auto itr = vzcomp::actors.find(it);
			auto itl = vzcomp::lights.find(it);
			auto itc = vzcomp::cameras.find(it);
			if (itr != vzcomp::actors.end())
				itr->second.get()->sceneVid = 0;
			else if (itl != vzcomp::lights.end())
				itl->second.get()->sceneVid = 0;
			else if (itc != vzcomp::cameras.end())
				itc->second.get()->sceneVid = 0;
			if (scene_src)
			{
				scene_src->Remove(it);
			}
		}

		if (scene_dst)
		{
			for (auto& it : entities_moving)
			{
				// The entity is ignored if it doesn't have a Renderable or Light component.
				scene_dst->AddEntity(it);

				auto itr = vzcomp::actors.find(it);
				auto itl = vzcomp::lights.find(it);
				auto itc = vzcomp::cameras.find(it);
				if (itr != vzcomp::actors.end())
					itr->second.get()->sceneVid = vid_scene_dst;
				if (itl != vzcomp::lights.end())
					itl->second.get()->sceneVid = vid_scene_dst;
				if (itc != vzcomp::cameras.end())
					itc->second.get()->sceneVid = vid_scene_dst;
			}
		}
		return true;
	}

	SceneVID AppendSceneCompVidTo(const VID vid, const VID parentVid)
	{
		CHECK_API_VALIDITY(INVALID_VID);

		if (!appendSceneEntityToParent(vid, parentVid))
		{
			return INVALID_VID;
		}

		Scene* scene = Scene::GetScene(parentVid); 
		if (scene)
		{
			return parentVid;
		}

		SceneVID vis_scene = INVALID_VID;
		auto itr = vzcomp::actors.find(parentVid);
		auto itl = vzcomp::lights.find(parentVid);
		auto itc = vzcomp::cameras.find(parentVid);
		if (itr != vzcomp::actors.end())
			vis_scene = itr->second.get()->sceneVid;
		if (itl != vzcomp::lights.end())
			vis_scene = itl->second.get()->sceneVid;
		if (itc != vzcomp::cameras.end())
			vis_scene = itc->second.get()->sceneVid;

		assert(vis_scene);
		return vis_scene;
	}

	VzScene* AppendSceneCompTo(const VZ_NONNULL VzBaseComp* comp, const VZ_NONNULL VzBaseComp* parentComp)
	{
		CHECK_API_VALIDITY(nullptr);
		Scene* scene = Scene::GetScene(AppendSceneCompVidTo(comp->GetVID(), parentComp ? parentComp->GetVID() : 0));
		auto it = vzcomp::scenes.find(scene->GetSceneEntity());
		if (it == vzcomp::scenes.end())
		{
			return nullptr;
		}
		return it->second.get();
	}

	bool DeinitEngineLib()
	{
		CHECK_API_VALIDITY(false);
		jobsystem::ShutDown();
		graphicsPackage.pluginDeinitializer();
		return true;
	}
}
