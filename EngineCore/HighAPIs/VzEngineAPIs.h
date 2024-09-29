#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	// This must be called before using engine APIs
	//  - paired with DeinitEngineLib()
	extern "C" API_EXPORT VZRESULT InitEngineLib(const vzm::ParamMap<std::string>& arguments = vzm::ParamMap<std::string>());
	extern "C" API_EXPORT VZRESULT DeinitEngineLib();

	// here... 
	// 1. Engine (De)Initialization
	// 2. System (ecS) functions
	// 3. Component (eCs) factory interfaces
	//   - Component-dependent interfaces are defined in each component (as static interfaces)
	// 4. VID is used instead of Entity (but they are same)
	// 

	// Get Entity ID 
	//  - return zero in case of failure 
	extern "C" API_EXPORT VID GetFirstVidByName(const std::string& name);
	// Get Entity IDs whose name is the input name (VID is allowed for redundant name)
	//  - return # of entities
	extern "C" API_EXPORT size_t GetVidsByName(const std::string& name, std::vector<VID>& vids);
	// Get Entity's name if possible
	//  - return name string if entity's name exists, if not, return "" 
	extern "C" API_EXPORT bool GetNameByVid(const VID vid, std::string& name);
	// Remove an entity (scene, scene components, renderer, asset) 
	extern "C" API_EXPORT void RemoveComponent(const VID vid);
	// Create new scene and return scene (NOT a scene item) ID, a scene 
	//  - return zero in case of failure (the name is already registered or overflow VID)
	extern "C" API_EXPORT VzScene* NewScene(const std::string& sceneName);
	extern "C" API_EXPORT VzRenderer* NewRenderer(const std::string& rendererName);
	// Create new scene component (SCENE_COMPONENT_TYPE::CAMERA, ACTOR, LIGHT) NOT SCENE_COMPONENT_TYPE::SCENEBASE
	//  - Must belong to a scene
	//  - parentVid cannot be a scene (renderable or 0)
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	extern "C" API_EXPORT VzSceneComp* NewSceneComponent(const SCENE_COMPONENT_TYPE compType, const std::string& compName, const VID parentVid = 0u);
	extern "C" API_EXPORT VzResource* NewResComponent(const RES_COMPONENT_TYPE compType, const std::string& compName);
	// Get Component and return its pointer registered in renderer
	//  - return nullptr in case of failure
	extern "C" API_EXPORT VzBaseComp* GetVzComponent(const VID vid);
	extern "C" API_EXPORT VzBaseComp* GetFirstVzComponentByName(const std::string& name);
	extern "C" API_EXPORT size_t GetVzComponentsByName(const std::string& name, std::vector<VzBaseComp*>& components);
	extern "C" API_EXPORT size_t GetVzComponentsByType(const std::string& type, std::vector<VzBaseComp*>& components);
	// Append Component to the parent component
	//  - in : VzSceneComp or VzScene
	//  - return sceneId containing the parent component 
	extern "C" API_EXPORT SceneVID AppendSceneCompVidTo(const VID vid, const VID parentVid);
	extern "C" API_EXPORT VzScene* AppendSceneCompTo(const VZ_NONNULL VzBaseComp* comp, const VZ_NONNULL VzBaseComp* parentComp);
	// Get Component IDs in a scene
	//  - return # of scene Components 
	extern "C" API_EXPORT size_t GetSceneCompoenentVids(const SCENE_COMPONENT_TYPE compType, const VID sceneVid, std::vector<VID>& vids, const bool isRenderableOnly = false);	// Get CameraParams and return its pointer registered in renderer
	// Load a mesh file (obj and stl) into actors and return the first actor
	//  - return zero in case of failure
	extern "C" API_EXPORT VzActor* LoadModelFileIntoActors(const std::string& filename, std::vector<VzActor*>& actors);
	extern "C" API_EXPORT float GetAsyncLoadProgress();
	// Get a graphics render target view 
	//  - Must belong to the internal scene
	extern "C" API_EXPORT void* GetGraphicsSharedRenderTarget();
	// Reload shaders
	extern "C" API_EXPORT void ReloadShader();
}
