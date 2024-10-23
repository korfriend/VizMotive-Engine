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
	API_EXPORT VzVolume* NewVolume(const std::string& name);

	// Append Component to the parent component
	//  - in : VzSceneComp or VzScene
	//  - return sceneId containing the parent component 
	API_EXPORT SceneVID AppendSceneCompVidTo(const VID vid, const VID parentVid);
	API_EXPORT VzScene* AppendSceneCompTo(const VZ_NONNULL VzBaseComp* comp, const VZ_NONNULL VzBaseComp* parentComp);

	// Get Entity ID 
	//  - return zero in case of failure 
	API_EXPORT VID GetFirstVidByName(const std::string& name);
	API_EXPORT VzBaseComp* GetFirstComponentByName(const std::string& name);
	// Get Entity IDs whose name is the input name (VID is allowed for redundant name)
	//  - return # of entities
	API_EXPORT size_t GetVidsByName(const std::string& name, std::vector<VID>& vids);
	API_EXPORT size_t GetComponentsByName(const std::string& name, std::vector<VzBaseComp*>& components);
	// Get Component and return its pointer registered in renderer
	//  - return nullptr in case of failure
	API_EXPORT VzBaseComp* GetComponent(const VID vid);

	// Get Entity's name if possible
	//  - return name string if entity's name exists, if not, return "" 
	API_EXPORT std::string GetNameByVid(const VID vid);
	// Remove an entity (scene, scene components, renderer, asset) 
	API_EXPORT bool RemoveComponent(const VID vid);
	API_EXPORT inline bool RemoveComponent(const VzBaseComp* comp) { return RemoveComponent(comp->GetVID()); }

	// Load a mesh file (obj and stl) into actors and return the first actor
	//  - return zero in case of failure
	API_EXPORT VzActor* LoadModelFileIntoActors(const std::string& filename, std::vector<VzActor*>& actors);
	API_EXPORT float GetAsyncLoadProgress();
	// Get a graphics render target view 
	//  - Must belong to the internal scene
	API_EXPORT void* GetGraphicsSharedRenderTarget();

	API_EXPORT bool ExecutePluginFunction(const std::string& pluginFilename, const std::string& functionName, ParamMap<std::string>& io);

	// Reload shaders
	API_EXPORT void ReloadShader();
}
