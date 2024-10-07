#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	// This must be called before using engine APIs
	//  - paired with DeinitEngineLib()
	API_EXPORT bool InitEngineLib(const vzm::ParamMap<std::string>& arguments = vzm::ParamMap<std::string>());
	API_EXPORT bool DeinitEngineLib();

	// here... 
	// 1. Engine (De)Initialization
	// 2. System (ecS) functions
	// 3. Component (eCs) factory interfaces
	//   - Component-dependent interfaces are defined in each component (as static interfaces)
	// 4. VID is used instead of Entity (but they are same)
	// 

	// Get Entity ID 
	//  - return zero in case of failure 
	API_EXPORT VID GetFirstVidByName(const std::string& name);
	// Get Entity IDs whose name is the input name (VID is allowed for redundant name)
	//  - return # of entities
	API_EXPORT size_t GetVidsByName(const std::string& name, std::vector<VID>& vids);
	// Get Entity's name if possible
	//  - return name string if entity's name exists, if not, return "" 
	API_EXPORT bool GetNameByVid(const VID vid, std::string& name);
	// Remove an entity (scene, scene components, renderer, asset) 
	API_EXPORT void RemoveComponent(const VID vid);
	// Create new scene and return scene (NOT a scene item) ID, a scene 
	//  - return zero in case of failure (the name is already registered or overflow VID)
	API_EXPORT VzScene* NewScene(const std::string& name);
	API_EXPORT VzRenderer* NewRenderer(const std::string& name);
	// Create new scene component (SCENE_COMPONENT_TYPE::CAMERA, ACTOR, LIGHT) NOT SCENE_COMPONENT_TYPE::SCENEBASE
	//  - Must belong to a scene
	//  - parentVid cannot be a scene (renderable or 0)
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	API_EXPORT VzCamera* NewCamera(const std::string& name, const VID parentVid = 0u);
	API_EXPORT VzActor* NewActor(const std::string& name, const GeometryVID vidGeo = 0u, const MaterialVID vidMat = 0u, const VID parentVid = 0u);
	API_EXPORT VzActor* NewActor(const std::string& name, const VzGeometry* geometry, const VzMaterial* material, const VID parentVid = 0u);
	API_EXPORT VzLight* NewLight(const std::string& name, const VID parentVid = 0u);

	API_EXPORT VzGeometry* NewGeometry(const std::string& name);
	API_EXPORT VzMaterial* NewMaterial(const std::string& name);
	API_EXPORT VzTexture* NewTexture(const std::string& name);
	// Get Component and return its pointer registered in renderer
	//  - return nullptr in case of failure
	API_EXPORT VzBaseComp* GetVzComponent(const VID vid);
	API_EXPORT VzBaseComp* GetFirstVzComponentByName(const std::string& name);
	API_EXPORT size_t GetVzComponentsByName(const std::string& name, std::vector<VzBaseComp*>& components);
	API_EXPORT size_t GetVzComponentsByType(const std::string& type, std::vector<VzBaseComp*>& components);
	// Append Component to the parent component
	//  - in : VzSceneComp or VzScene
	//  - return sceneId containing the parent component 
	API_EXPORT SceneVID AppendSceneCompVidTo(const VID vid, const VID parentVid);
	API_EXPORT VzScene* AppendSceneCompTo(const VZ_NONNULL VzBaseComp* comp, const VZ_NONNULL VzBaseComp* parentComp);
	// Get Component IDs in a scene
	//  - return # of scene Components 
	API_EXPORT size_t GetSceneCompoenentVids(const SCENE_COMPONENT_TYPE compType, const VID sceneVid, std::vector<VID>& vids, const bool isRenderableOnly = false);	// Get CameraParams and return its pointer registered in renderer
	// Load a mesh file (obj and stl) into actors and return the first actor
	//  - return zero in case of failure
	API_EXPORT VzActor* LoadModelFileIntoActors(const std::string& filename, std::vector<VzActor*>& actors);
	API_EXPORT float GetAsyncLoadProgress();
	// Get a graphics render target view 
	//  - Must belong to the internal scene
	API_EXPORT void* GetGraphicsSharedRenderTarget();
	// Reload shaders
	API_EXPORT void ReloadShader();
}
