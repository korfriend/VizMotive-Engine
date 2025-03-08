#include "VzEngineAPIs.h"
#include "Common/Engine_Internal.h"
#include "Common/RenderPath3D.h"
#include "Common/Initializer.h"
#include "Common/Config.h"
#include "Utils/Backlog.h"
#include "Utils/Platform.h"
#include "Utils/EventHandler.h"
#include "Utils/ECS.h"
#include "Utils/Helpers.h"
#include "Utils/PrivateInterface.h"
#include "GBackend/GBackendDevice.h"
#include "GBackend/GModuleLoader.h"

#include <sstream>

namespace vz
{
	GBackendLoader graphicsBackend;
	GShaderEngineLoader shaderEngine;
	std::unordered_map<std::string, HMODULE> importedModules;

	graphics::GraphicsDevice* graphicsDevice = nullptr;
	config::File configFile;
}

namespace vzcompmanager
{
	using namespace vz;
	using namespace vzm;
	// all are VzBaseComp (node-based)
	std::unordered_map<VID, VzBaseComp*> lookup;

	std::unordered_map<ArchiveVID, std::unique_ptr<VzArchive>> archives;

	std::unordered_map<SceneVID, std::unique_ptr<VzScene>> scenes;

	std::unordered_map<RendererVID, std::unique_ptr<VzRenderer>> renderers;

	std::unordered_map<CamVID, std::unique_ptr<VzCamera>> cameras;
	std::unordered_map<ActorVID, std::unique_ptr<VzActor>> actors;
	std::unordered_map<LightVID, std::unique_ptr<VzLight>> lights;

	std::unordered_map<GeometryVID, std::unique_ptr<VzGeometry>> geometries;
	std::unordered_map<MaterialVID, std::unique_ptr<VzMaterial>> materials;
	std::unordered_map<TextureVID, std::unique_ptr<VzTexture>> textures;
	std::unordered_map<VolumeVID, std::unique_ptr<VzVolume>> volumes;

	bool Destroy(const VID vid, const bool includeDescendants = false)
	{
		jobsystem::WaitAllJobs();

		auto it = lookup.find(vid);
		if (it == lookup.end())
		{
			return false;
		}

		if (graphicsDevice)
		{
			graphicsDevice->WaitForGPU();
		}

		VzBaseComp* vcomp = it->second;
		COMPONENT_TYPE comp_type = vcomp->GetType();
		bool is_engine_component = true;
		switch (comp_type)
		{
		case COMPONENT_TYPE::ARCHIVE: archives.erase(vid); Archive::DestroyArchive(vid); is_engine_component = false;  break;
		case COMPONENT_TYPE::SCENE: scenes.erase(vid); Scene::DestroyScene(vid); is_engine_component = false; break;
		case COMPONENT_TYPE::RENDERER: renderers.erase(vid); canvas::DestroyCanvas(vid); is_engine_component = false; break;
		case COMPONENT_TYPE::CAMERA:
		case COMPONENT_TYPE::SLICER:
			cameras.erase(vid); break;
		case COMPONENT_TYPE::ACTOR: actors.erase(vid); break;
		case COMPONENT_TYPE::LIGHT: lights.erase(vid); break;
		case COMPONENT_TYPE::GEOMETRY: geometries.erase(vid); break;
		case COMPONENT_TYPE::MATERIAL: materials.erase(vid); break;
		case COMPONENT_TYPE::TEXTURE: textures.erase(vid); break;
		default:
			assert(0);
		}

		if (is_engine_component)
		{
			if (includeDescendants)
			{
				HierarchyComponent* hierarchy = compfactory::GetHierarchyComponent(vid);
				for (VUID vuid : hierarchy->GetChildren())
				{
					HierarchyComponent* child = compfactory::GetHierarchyComponentByVUID(vuid);
					vzlog_assert(child, "a child MUST exist!");
					Destroy(child->GetEntity(), true);
				}
			}

			compfactory::Destroy(vid);
			Scene::RemoveEntityForScenes(vid);
		}
		lookup.erase(it);

		return true;
	}

	void DestroyAll()
	{
		jobsystem::WaitAllJobs();

		archives.clear();
		scenes.clear();
		renderers.clear();
		cameras.clear();
		actors.clear();
		lights.clear();
		geometries.clear();
		materials.clear();
		textures.clear();
		lookup.clear();

		Archive::DestroyAll();
		Scene::DestroyAll();

		canvas::DestroyAll();
		compfactory::DestroyAll();
	}
}

namespace vzm
{
	using namespace vz;

#define CHECK_API_INIT_VALIDITY(RET) if (!initialized) { vzlog_error("High-level API is not initialized!!"); return RET;}
#define CHECK_API_LOCKGUARD_VALIDITY(RET) CHECK_API_INIT_VALIDITY(RET); std::lock_guard<std::recursive_mutex> lock(GetEngineMutex());
#define CHECK_API_SINGLETHREAD_VALIDITY(RET) CHECK_API_INIT_VALIDITY(RET); vzlog_assert(engineThreadId == std::this_thread::get_id(), "The API must be called on the same thread that called InitEngineLib!");

	bool initialized = false;
	std::recursive_mutex& GetEngineMutex()
	{
		static std::recursive_mutex  engineMutex;
		return engineMutex;
	}

	std::atomic_bool isPandingSubmitCommand{ false };
	bool IsPendingSubmitCommand()
	{
		return isPandingSubmitCommand.load();
	}

	std::atomic<uint32_t> countPendingSubmitCommand{ 0u };
	void ResetPendingSubmitCommand()
	{
		isPandingSubmitCommand.store(false);
		countPendingSubmitCommand.store(0u);
	}
	void CountPendingSubmitCommand()
	{
		countPendingSubmitCommand.fetch_add(1);
	}
	size_t GetCountPendingSubmitCommand()
	{
		return countPendingSubmitCommand.load();
	}

	std::thread::id engineThreadId;
	inline uint64_t threadToInteger(const std::thread::id& id) {
		std::stringstream ss;
		ss << id;
		uint64_t result;
		ss >> result;
		return result;
	}

	VzSceneComp* newSceneComponent(const COMPONENT_TYPE compType, const std::string& compName, const VID parentVid, const uint64_t userData = 0)
	{
		CHECK_API_LOCKGUARD_VALIDITY(nullptr);
		switch (compType)
		{
		case COMPONENT_TYPE::ACTOR:
		case COMPONENT_TYPE::LIGHT:
		case COMPONENT_TYPE::CAMERA:
		case COMPONENT_TYPE::SLICER:
			break;
		default:
			vzlog_assert(0, "Invalid COMPONENT type for newSceneComponent!");
			return nullptr;
		}

		Entity entity = ecs::CreateEntity();

		compfactory::CreateNameComponent(entity, compName);
		compfactory::CreateTransformComponent(entity);
		compfactory::CreateHierarchyComponent(entity);

		VID vid = entity;
		VzSceneComp* hlcomp = nullptr;
		
		VID parent_vid = parentVid;
		switch (compType)
		{
		case COMPONENT_TYPE::ACTOR:
			compfactory::CreateRenderableComponent(entity);
			{
				auto it = vzcompmanager::actors.emplace(vid, std::make_unique<VzActor>(vid, "vzm::NewActor"));
				hlcomp = (VzSceneComp*)it.first->second.get();
			}
			break;
		case COMPONENT_TYPE::LIGHT:
			compfactory::CreateLightComponent(entity);
			{
				auto it = vzcompmanager::lights.emplace(vid, std::make_unique<VzLight>(vid, "vzm::NewLight"));
				hlcomp = (VzSceneComp*)it.first->second.get();
			}
			break;
		case COMPONENT_TYPE::CAMERA:
			compfactory::CreateCameraComponent(entity);
			{
				auto it = vzcompmanager::cameras.emplace(vid, std::make_unique<VzCamera>(vid, "vzm::NewCamera"));
				hlcomp = (VzSceneComp*)it.first->second.get();
			}
			break;
		case COMPONENT_TYPE::SLICER:
			compfactory::CreateSlicerComponent(entity, userData != 0);
			{
				auto it = vzcompmanager::cameras.emplace(vid, std::make_unique<VzSlicer>(vid, "vzm::NewSlicer"));
				hlcomp = (VzSceneComp*)it.first->second.get();

				if (userData != 0 && parent_vid != 0)
				{
					vzlog_assert(0, "Curved Slicer is NOT allow to have a parent! Force to set Null parent!");
					parent_vid = 0u;
				}
			}
			break;
		default:
			backlog::post("vzm::NewSceneComponent >> Invalid COMPONENT_TYPE", backlog::LogLevel::Error);
			return nullptr;
		}

		if (parentVid != INVALID_VID)
		{
			AppendSceneCompVidTo(vid, parent_vid);
		}

		vzcompmanager::lookup[vid] = hlcomp;
		return hlcomp;
	}

	VzResource* newResComponent(const COMPONENT_TYPE compType, const std::string& compName)
	{
		CHECK_API_LOCKGUARD_VALIDITY(nullptr);
		switch (compType)
		{
		case COMPONENT_TYPE::GEOMETRY:
		case COMPONENT_TYPE::MATERIAL:
		case COMPONENT_TYPE::TEXTURE:
		case COMPONENT_TYPE::VOLUME:
			break;
		default:
			return nullptr;
		}

		Entity entity = ecs::CreateEntity();

		compfactory::CreateNameComponent(entity, compName);

		VID vid = entity;
		VzResource* hlcomp = nullptr;

		switch (compType)
		{
		case COMPONENT_TYPE::GEOMETRY:
			compfactory::CreateGeometryComponent(entity);
			{
				auto it = vzcompmanager::geometries.emplace(vid, std::make_unique<VzGeometry>(vid, "vzm::NewGeometry"));
				hlcomp = (VzGeometry*)it.first->second.get();
			}
			break;
		case COMPONENT_TYPE::MATERIAL:
			compfactory::CreateMaterialComponent(entity);
			{
				auto it = vzcompmanager::materials.emplace(vid, std::make_unique<VzMaterial>(vid, "vzm::NewMaterial"));
				hlcomp = (VzMaterial*)it.first->second.get();
			}
			break;
		case COMPONENT_TYPE::TEXTURE:
			compfactory::CreateTextureComponent(entity);
			{
				auto it = vzcompmanager::textures.emplace(vid, std::make_unique<VzTexture>(vid, "vzm::NewTexture"));
				hlcomp = (VzTexture*)it.first->second.get();
			}
			break;
		case COMPONENT_TYPE::VOLUME:
			compfactory::CreateVolumeComponent(entity);
			{
				auto it = vzcompmanager::volumes.emplace(vid, std::make_unique<VzVolume>(vid, "vzm::NewVolume"));
				hlcomp = (VzVolume*)it.first->second.get();
			}
			break;
		default:
			backlog::post("vzm::NewResComponent >> Invalid COMPONENT_TYPE", backlog::LogLevel::Error);
			return nullptr;
		}
		vzcompmanager::lookup[vid] = hlcomp;
		return hlcomp;
	}
}

namespace vzm
{
	bool InitEngineLib(const vzm::ParamMap<std::string>& arguments)
	{
		std::lock_guard<std::recursive_mutex> lock(GetEngineMutex());
		engineThreadId = std::this_thread::get_id();
		vzlog("Engine API's thread is assigned to thread ID (%lld)", threadToInteger(engineThreadId));

#ifdef PLATFORM_WINDOWS_DESKTOP
#if defined(_DEBUG) && defined(_MT_LEAK_CHECK)
		_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
#endif
		if (initialized)
		{
			backlog::post("Already initialized!", backlog::LogLevel::Warn);
			return false;
		}
		
		std::string ems_string = "ENGINE_MANAGER_SETTINGS";
		const char* ems_string_c = ems_string.c_str();
		{
			// CONFIG FILE
			// backlog has been already initialized!
			std::string config_filename = arguments.GetString("CONFIG", std::string(backlog::GetLogPath()) + "vzConfig.ini");
			if (!helper::FileExists(config_filename))
			{
				std::ofstream file(config_filename);
				file.close();
			}
			assert(configFile.Open(config_filename.c_str()));
			config::Section& section = configFile.GetSection(ems_string_c);
			if (!section.Has("API"))
			{
				section.Set("API", "DX12");
			}
			if (!section.Has("GPU_VALIDATION"))
			{
				section.Set("GPU_VALIDATION", "DISABLED");
			}
			if (!section.Has("GPU_PREFERENCE"))
			{
				section.Set("GPU_PREFERENCE", "DISCRETE");
			}
			if (!section.Has("MAX_THREADS"))
			{
				section.Set("MAX_THREADS", "MAXIMUM");
			}
			if (!section.Has("TARGET_FPS"))
			{
				section.Set("TARGET_FPS", -1);
			}
			configFile.Commit();
		}

		config::Section& section = configFile.GetSection(ems_string_c);
		std::string api = "DX12";
		// assume DX12 rendering engine
		if (arguments.FindParam("API"))
		{
			api = arguments.GetString("API", "DX12");
		}
		else
		{
			if (section.Has("API"))
			{
				api = section.GetText("API");
			}
		}
		if (!graphicsBackend.Init(api))
		{
			vzlog_error("Invalid Graphics API : %s", api.c_str());
			return false;
		}
		if (!shaderEngine.Init(api == "DX11" ? "ShaderEngineDX11" : "ShaderEngine"))
		{
			vzlog_error("Invalid Shader Engine");
			return false;
		}

		if (int targetFPS = section.GetInt("TARGET_FPS") > 0)
		{
			RenderPath::framerateLock = true;
			RenderPath::targetFrameRate = (float)targetFPS;
		}
		else
		{
			RenderPath::framerateLock = false;
		}

		// initialize the graphics backend
		graphics::ValidationMode validationMode = graphics::ValidationMode::Disabled;
		std::string validation = "DISABLED";
		if (arguments.FindParam("DISABLED"))
		{
			validation = arguments.GetString("GPU_VALIDATION", "DISABLED");
		}
		else
		{
			config::Section& section = configFile.GetSection(ems_string_c);
			if (section.Has("GPU_VALIDATION"))
			{
				validation = section.GetText("GPU_VALIDATION");
			}
		}
		if (validation == "VERBOSE")
		{
			validationMode = graphics::ValidationMode::Verbose;
		}
#ifdef _DEBUG
		//validationMode = graphics::ValidationMode::Verbose;
#endif
		if (validationMode == graphics::ValidationMode::Verbose)
		{
			vzlog_warning("GPU Devide Debug Layer: ON >> Performance will be seriously downgraded with some memory-leakage-like profiles!!")
		}

		graphics::GPUPreference preferenceMode = graphics::GPUPreference::Discrete;
		std::string preference = "DISCRETE";
		if (arguments.FindParam("GPU_PREFERENCE"))
		{
			preference = arguments.GetString("GPU_PREFERENCE", "DISCRETE");
		}
		else
		{
			config::Section& section = configFile.GetSection(ems_string_c);
			if (section.Has("GPU_PREFERENCE"))
			{
				preference = section.GetText("GPU_PREFERENCE");
			}
		}
		if (preference == "INTEGRATED")
		{
			preferenceMode = graphics::GPUPreference::Integrated;
		}
		graphicsBackend.pluginInitializer(validationMode, preferenceMode);

		// graphics device
		graphicsDevice = graphicsBackend.pluginGetDev();
		graphics::GetDevice() = graphicsDevice;
		shaderEngine.pluginInitializer(graphicsDevice);

		// engine core initializer

		uint32_t num_max_threads = ~0u;
		if (arguments.FindParam("MAX_THREADS"))
		{
			num_max_threads = arguments.GetParam("MAX_THREADS", ~0u);
		}
		else
		{
			config::Section& section = configFile.GetSection(ems_string_c);
			if (section.Has("MAX_THREADS"))
			{
				std::string max_thread_str = section.GetText("MAX_THREADS");
				if (max_thread_str != "MAXIMUM")
				{
					try {
						num_max_threads = std::stoi(max_thread_str);
					}
					catch (const std::invalid_argument& e) {
						vzlog_warning(e.what());
					}
					catch (const std::out_of_range& e) {
						vzlog_warning(e.what());
					}
				}
			}
		}

		initializer::SetMaxThreadCount(num_max_threads);
		initializer::InitializeComponentsAsync();	// involving jobsystem initializer
		
		initialized = true;
		return true;
	}

	bool IsValidEngineLib()
	{
		return initialized;
	}

	VzArchive* NewArchive(const std::string& name)
	{
		CHECK_API_LOCKGUARD_VALIDITY(nullptr);
		Archive* archive = Archive::CreateArchive(name); // lock_guard (recursive mutex)
		ArchiveVID vid = archive->GetArchiveEntity();
		vid = archive->GetArchiveEntity();
		auto it = vzcompmanager::archives.emplace(vid, std::make_unique<VzArchive>(vid, "vzm::NewArchive"));
		compfactory::CreateNameComponent(vid, name); // lock_guard (recursive mutex)
		vzcompmanager::lookup[vid] = it.first->second.get();
		return it.first->second.get();
	}
	VzScene* NewScene(const std::string& name)
	{
		CHECK_API_LOCKGUARD_VALIDITY(nullptr);
		Scene* scene = Scene::CreateScene(name);
		SceneVID vid = scene->GetSceneEntity();
		auto it = vzcompmanager::scenes.emplace(vid, std::make_unique<VzScene>(vid, "vzm::NewScene"));
		compfactory::CreateNameComponent(vid, name);
		vzcompmanager::lookup[vid] = it.first->second.get();
		return it.first->second.get();
	}
	VzRenderer* NewRenderer(const std::string& name)
	{
		CHECK_API_LOCKGUARD_VALIDITY(nullptr);
		RenderPath3D* renderer = canvas::CreateRenderPath3D(graphicsDevice, name);
		RendererVID vid = renderer->GetEntity();
		auto it = vzcompmanager::renderers.emplace(vid, std::make_unique<VzRenderer>(vid, "vzm::NewRenderer"));
		compfactory::CreateNameComponent(vid, name);
		vzcompmanager::lookup[vid] = it.first->second.get();
		return it.first->second.get();
	}

	VzCamera* NewCamera(const std::string& name, const VID parentVid)
	{
		return (VzCamera*)newSceneComponent(COMPONENT_TYPE::CAMERA, name, parentVid);
	}
	VzSlicer* NewSlicer(const std::string& name, const bool curvedSlicer, const VID parentVid)
	{
		return (VzSlicer*)newSceneComponent(COMPONENT_TYPE::SLICER, name, parentVid, curvedSlicer? 1 : 0);
	}
	VzActor* NewActor(const std::string& name, const GeometryVID vidGeo, const MaterialVID vidMat, const VID parentVid)
	{
		VzActor* actor = (VzActor*)newSceneComponent(COMPONENT_TYPE::ACTOR, name, parentVid);
		if (vidGeo) actor->SetGeometry(vidGeo);
		if (vidMat) actor->SetMaterial(vidMat);
		return actor;
	}
	VzActor* NewActor(const std::string& name, const VzGeometry* geometry, const VzMaterial* material, const VID parentVid)
	{
		return NewActor(name, geometry? geometry->GetVID() : 0u, material? material->GetVID() : 0u, parentVid);
	}
	VzLight* NewLight(const std::string& name, const VID parentVid)
	{
		return (VzLight*)newSceneComponent(COMPONENT_TYPE::LIGHT, name, parentVid);
	}

	VzGeometry* NewGeometry(const std::string& name)
	{
		return (VzGeometry*)newResComponent(COMPONENT_TYPE::GEOMETRY, name);
	}
	VzMaterial* NewMaterial(const std::string& name)
	{
		return (VzMaterial*)newResComponent(COMPONENT_TYPE::MATERIAL, name);
	}
	VzTexture* NewTexture(const std::string& name)
	{
		return (VzTexture*)newResComponent(COMPONENT_TYPE::TEXTURE, name);
	}
	VzVolume* NewVolume(const std::string& name)
	{
		return (VzVolume*)newResComponent(COMPONENT_TYPE::VOLUME, name);
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
					auto itr = vzcompmanager::actors.find(vid);
					auto itl = vzcompmanager::lights.find(vid);
					auto itc = vzcompmanager::cameras.find(vid);
					if (itr == vzcompmanager::actors.end()
						&& itl == vzcompmanager::lights.end()
						&& itc == vzcompmanager::cameras.end())
					{
						vid_scene = INVALID_VID;
					}
					else
					{
						vid_scene = std::max(
							std::max(itl != vzcompmanager::lights.end() ? itl->second.get()->sceneVid : INVALID_VID,
								itr != vzcompmanager::actors.end() ? itr->second.get()->sceneVid : INVALID_VID), 
							itc != vzcompmanager::cameras.end() ? itc->second.get()->sceneVid : INVALID_VID 
						);
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
			Scene::DestroyScene(vid_scene_src);
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

			Scene::DestroyScene(vid_scene_src);
			scene_src = nullptr;
		}

		// NOTE 
		// a scene can have entities with components that are not renderable
		// they are supposed to be ignored during the rendering pipeline

		for (auto& it : entities_moving)
		{
			auto itr = vzcompmanager::actors.find(it);
			auto itl = vzcompmanager::lights.find(it);
			auto itc = vzcompmanager::cameras.find(it);
			if (itr != vzcompmanager::actors.end())
				itr->second.get()->sceneVid = 0;
			else if (itl != vzcompmanager::lights.end())
				itl->second.get()->sceneVid = 0;
			else if (itc != vzcompmanager::cameras.end())
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

				auto itr = vzcompmanager::actors.find(it);
				auto itl = vzcompmanager::lights.find(it);
				auto itc = vzcompmanager::cameras.find(it);
				if (itr != vzcompmanager::actors.end())
					itr->second.get()->sceneVid = vid_scene_dst;
				if (itl != vzcompmanager::lights.end())
					itl->second.get()->sceneVid = vid_scene_dst;
				if (itc != vzcompmanager::cameras.end())
					itc->second.get()->sceneVid = vid_scene_dst;
			}
		}
		return true;
	}

	SceneVID AppendSceneCompVidTo(const VID vid, const VID parentVid)
	{
		CHECK_API_LOCKGUARD_VALIDITY(INVALID_VID);

		if (!appendSceneEntityToParent(vid, parentVid))
		{
			return INVALID_VID;
		}

		Scene* scene = Scene::GetScene(parentVid); 
		if (scene)
		{
			return parentVid;
		}

		SceneVID vid_scene = INVALID_VID;
		auto itr = vzcompmanager::actors.find(vid);
		auto itl = vzcompmanager::lights.find(vid);
		auto itc = vzcompmanager::cameras.find(vid);
		if (itr != vzcompmanager::actors.end())
			vid_scene = itr->second.get()->sceneVid;
		if (itl != vzcompmanager::lights.end())
			vid_scene = itl->second.get()->sceneVid;
		if (itc != vzcompmanager::cameras.end())
			vid_scene = itc->second.get()->sceneVid;

		return vid_scene;
	}

	VzScene* AppendSceneCompTo(const VZ_NONNULL VzBaseComp* comp, const VZ_NONNULL VzBaseComp* parentComp)
	{
		Scene* scene = Scene::GetScene(AppendSceneCompVidTo(comp->GetVID(), parentComp ? parentComp->GetVID() : 0));
		auto it = vzcompmanager::scenes.find(scene->GetSceneEntity());
		if (it == vzcompmanager::scenes.end())
		{
			return nullptr;
		}
		return it->second.get();
	}

	VID GetFirstVidByName(const std::string& name)
	{
		CHECK_API_INIT_VALIDITY(INVALID_VID);
		return compfactory::GetFirstEntityByName(name);
	}
	VzBaseComp* GetFirstComponentByName(const std::string& name)
	{
		CHECK_API_INIT_VALIDITY(nullptr);
		VID vid = compfactory::GetFirstEntityByName(name);
		if (vid == INVALID_VID)
		{
			return nullptr;
		}
		auto it = vzcompmanager::lookup.find(vid);
		assert(it != vzcompmanager::lookup.end());
		return it->second;
	}

	size_t GetVidsByName(const std::string& name, std::vector<VID>& vids)
	{
		CHECK_API_INIT_VALIDITY(0);
		return compfactory::GetEntitiesByName(name, vids);
	}

	size_t GetComponentsByName(const std::string& name, std::vector<VzBaseComp*>& components)
	{
		CHECK_API_INIT_VALIDITY(0);
		components.clear();
		std::vector<VID> vids;
		size_t n = compfactory::GetEntitiesByName(name, vids);
		if (n == 0)
		{
			return 0;
		}
		
		components.reserve(n);
		for (size_t i = 0; i < n; ++i)
		{
			auto it = vzcompmanager::lookup.find(vids[i]);
			assert(it != vzcompmanager::lookup.end());
			components.push_back(it->second);
		}
		return n;
	}

	std::string GetNameByVid(const VID vid)
	{
		CHECK_API_INIT_VALIDITY("");
		NameComponent* name_comp = compfactory::GetNameComponent(vid);
		return name_comp ? name_comp->GetName() : "";
	}

	VzBaseComp* GetComponent(const VID vid)
	{
		CHECK_API_INIT_VALIDITY(nullptr);
		auto it = vzcompmanager::lookup.find(vid);
		return it == vzcompmanager::lookup.end() ? nullptr : it->second;
	}
	
	bool RemoveComponent(const VID vid, const bool includeDescendants)
	{
		CHECK_API_LOCKGUARD_VALIDITY(false);
		return vzcompmanager::Destroy(vid, includeDescendants);	// jobsystem
	}

	VzActor* LoadModelFile(const std::string& filename)
	{
		CHECK_API_INIT_VALIDITY(nullptr);

		typedef Entity(*PI_Function)(const std::string& fileName);

		PI_Function lpdll_function = platform::LoadModule<PI_Function>("AssetIO", "ImportModel_OBJ", importedModules);
		if (lpdll_function == nullptr)
		{
			backlog::post("vzm::LoadModelFile >> Invalid plugin function!", backlog::LogLevel::Error);
			return nullptr;
		}
		Entity root_entity = lpdll_function(filename);

		auto it = vzcompmanager::actors.find(root_entity);
		assert(it != vzcompmanager::actors.end());
		return it->second.get();
	}

	bool ExecutePluginFunction(const std::string& pluginFilename, const std::string& functionName, ParamMap<std::string>& io)
	{
		CHECK_API_INIT_VALIDITY(false);
		typedef bool(*PI_Function)(std::unordered_map<std::string, std::any>& io);
		PI_Function lpdll_function = platform::LoadModule<PI_Function>(pluginFilename, functionName, importedModules);
		if (lpdll_function == nullptr)
		{
			backlog::post("vzm::ExecutePluginFunction >> Invalid plugin function!", backlog::LogLevel::Error);
			return false;
		}
		std::unordered_map<std::string, std::any>& io_map = io.GetMap();
		return lpdll_function(io_map);
	}

	void PendingSubmitCommand(const bool pending)
	{
		CHECK_API_LOCKGUARD_VALIDITY(;);
		isPandingSubmitCommand.store(pending);
	}

	void ReloadShader()
	{
		CHECK_API_LOCKGUARD_VALIDITY(;);
		eventhandler::FireEvent(eventhandler::EVENT_RELOAD_SHADERS, 0);
		graphicsDevice->ClearPipelineStateCache();
	}

	bool DeinitEngineLib()
	{
		CHECK_API_INIT_VALIDITY(false);
		graphicsDevice->WaitForGPU();
		jobsystem::ShutDown();

		// lock_gaurd MUST be placed AFTER jobsystem::ShutDown() or WaitAllJobs()!
		std::lock_guard<std::recursive_mutex> lock(GetEngineMutex());
		graphicsDevice->WaitForGPU();	// double check for safe

		// high-level apis handle engine components via functions defined in vzcomp namespace
		vzcompmanager::DestroyAll();	// here, after-shutdown drives a single threaded process

		graphicsBackend.pluginDeinitializer();
		shaderEngine.pluginDeinitializer();
		
		eventhandler::Destroy();

		vzlog("\n\n Safely Engine Finished!\n ---------------------------- \n Bye Bye ^^! \n ----------------------------");
		backlog::Destroy();

		return true;
	}
}

namespace vz::compfactory
{
	using namespace vzm;

#define DEFINE_NEW_NODE_FUNC(TYPE) \
        VzBaseComp* comp = New##TYPE(name, parentEntity); \
        return comp ? comp->GetVID() : INVALID_ENTITY; 

#define DEFINE_NEW_RES_FUNC(TYPE) \
        VzBaseComp* comp = New##TYPE(name); \
        return comp ? comp->GetVID() : INVALID_ENTITY; 

	Entity NewNodeActor(const std::string& name, const Entity parentEntity)	
	{
		VzBaseComp* comp = NewActor(name, 0u, 0u, parentEntity);
		return comp ? comp->GetVID() : INVALID_ENTITY;
	}
	Entity NewNodeCamera(const std::string& name, const Entity parentEntity) { DEFINE_NEW_NODE_FUNC(Camera) }
	Entity NewNodeSlicer(const std::string& name, const bool curvedSlicer, const Entity parentEntity) 
	{
		VzBaseComp* comp = NewSlicer(name, curvedSlicer, parentEntity);
		return comp ? comp->GetVID() : INVALID_ENTITY;
	}
	Entity NewNodeLight(const std::string& name, const Entity parentEntity) { DEFINE_NEW_NODE_FUNC(Light) }
	Entity NewResGeometry(const std::string& name) { DEFINE_NEW_RES_FUNC(Geometry) }
	Entity NewResMaterial(const std::string& name) { DEFINE_NEW_RES_FUNC(Material) }
	Entity NewResTexture(const std::string& name) { DEFINE_NEW_RES_FUNC(Texture) }
	Entity NewResVolume(const std::string& name) { DEFINE_NEW_RES_FUNC(Volume) }

	size_t RemoveEntity(const Entity entity, const bool includeDescendants)
	{
		VzBaseComp* comp = GetComponent(entity);
		if (comp == nullptr) {
			vzlog_error("Invalid Enitity!");
			return 0;
		}
		switch (comp->GetType())
		{
		case COMPONENT_TYPE::ARCHIVE:
		case COMPONENT_TYPE::SCENE:
		case COMPONENT_TYPE::RENDERER:
		case COMPONENT_TYPE::UNDEF:
			vzlog_error("Not Allowed Type! COMPONENT_TYPE(%d)", (int)comp->GetType());
			return 0;
		default:
			break;
		}
		return RemoveComponent(entity, includeDescendants);
	}
}