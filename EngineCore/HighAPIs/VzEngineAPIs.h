#pragma once
#include "VzComponentAPIs.h"

// This high-level interfaces provide: 
// 1. Engine (De)Initialization
// 2. System (ecS) functions
// 3. Component (eCs) factory interfaces
//   - Component-dependent interfaces are defined in each component (as static interfaces)
// 4. VID is used instead of Entity (but they are same)

namespace vzm
{
	// This must be called before using engine APIs
	//  - paired with DeinitEngineLib()
	//  - "GPU_VALIDATION" note: only one debug_layer is available 
	//		unless the other device previously created will be removed unexpectedly!
	API_EXPORT bool InitEngineLib(const vzm::ParamMap<std::string>& arguments = vzm::ParamMap<std::string>());
	API_EXPORT bool DeinitEngineLib();
	API_EXPORT bool IsValidEngineLib();

	API_EXPORT VzArchive* NewArchive(const std::string& name);
	// Create new scene and return scene (NOT a scene item) ID, a scene 
	//  - return zero in case of failure (the name is already registered or overflow VID)
	API_EXPORT VzScene* NewScene(const std::string& name);
	// Create new renderer and return renderer ID, a renderer
	//  - renderer has canvas which involves rendertarget buffers and textures
	//  - return zero in case of failure (the name is already registered or overflow VID)
	API_EXPORT VzRenderer* NewRenderer(const std::string& name);

	// Create new scene component (CAMERA, ACTOR, LIGHT) INCLUDE HIERARCHY and TRANSFORMS
	//  - Must belong to a scene
	//  - parentVid cannot be a scene (renderable or 0)
	//  - return the generated scene component or nullptr in case of failure (invalid sceneID or overflow VID)
	API_EXPORT VzActor* NewActorNode(const std::string& name, VID parentVid = 0u);
	API_EXPORT VzCamera* NewCamera(const std::string& name, const VID parentVid = 0u);
	API_EXPORT VzSlicer* NewSlicer(const std::string& name, const bool curvedSlicer, const VID parentVid = 0u);
	API_EXPORT VzActorStaticMesh* NewActorStaticMesh(const std::string& name, const GeometryVID vidGeo = 0u, const MaterialVID vidMat = 0u, const VID parentVid = 0u);
	API_EXPORT VzActorGSplat* NewActorGSplat(const std::string& name, const GeometryVID vidGeo = 0u, const MaterialVID vidMat = 0u, const VID parentVid = 0u);
	API_EXPORT VzActorVolume* NewActorVolume(const std::string& name, const MaterialVID vidMat = 0u, const VID parentVid = 0u);
	API_EXPORT VzActorSprite* NewActorSprite(const std::string& name, const VID parentVid = 0u);
	API_EXPORT VzActorSpriteFont* NewActorSpriteFont(const std::string& name, const VID parentVid = 0u);
	API_EXPORT VzLight* NewLight(const std::string& name, const VID parentVid = 0u);

	// Create new resource component (GEOMETRY, MATERIAL, TEXTURE, VOLUME) NOT INCLUDE HIERARCHY and TRANSFORMS
	//  - return zero in case of failure (overflow VID)
	API_EXPORT VzGeometry* NewGeometry(const std::string& name);
	API_EXPORT VzMaterial* NewMaterial(const std::string& name);
	API_EXPORT VzTexture* NewTexture(const std::string& name);
	API_EXPORT VzVolume* NewVolume(const std::string& name);

	// Append Component to the parent component
	//  - in : VzSceneComp or VzScene for both vid and parentVid
	//  - return scene containing the parent component or nullptr in case of failure (invalid input VIDs)
	API_EXPORT SceneVID AppendSceneCompVidTo(const VID vid, const VID parentVid);
	API_EXPORT VzScene* AppendSceneCompTo(const VZ_NONNULL VzBaseComp* comp, const VZ_NONNULL VzBaseComp* parentComp);

	// Get Entity ID 
	//  - return zero in case of failure 
	API_EXPORT VID GetFirstVidByName(const std::string& name);
	API_EXPORT VzBaseComp* GetFirstComponentByName(const std::string& name);
	// Get Entity IDs whose name is the input name (VID is allowed for redundant name)
	//  - return # of engine-level ECS-based components
	API_EXPORT size_t GetVidsByName(const std::string& name, std::vector<VID>& vids);
	API_EXPORT size_t GetComponentsByName(const std::string& name, std::vector<VzBaseComp*>& components);
	// Get Component and return its pointer registered in renderer
	//  - return nullptr in case of failure
	API_EXPORT VzBaseComp* GetComponent(const VID vid);

	API_EXPORT size_t GetVidsByType(const COMPONENT_TYPE type, std::vector<VID>& vids);
	API_EXPORT size_t GetComponentsByType(const COMPONENT_TYPE type, std::vector<VzBaseComp*>& components);

	// Get Entity's name if possible
	//  - return name string if entity's name exists, if not, return "" 
	API_EXPORT std::string GetNameByVid(const VID vid);
	// Remove an entity (scene, scene components, renderer, asset) 
	API_EXPORT bool RemoveComponent(const VID vid, const bool includeDescendants = false);
	inline bool RemoveComponent(const VzBaseComp* comp, const bool includeDescendants = false) { return RemoveComponent(comp->GetVID(), includeDescendants); }

	size_t GetResourceManagerUsageCPU(std::unordered_map<std::string, size_t>& usageMap);
	size_t GetResourceManagerUsageGPU(std::unordered_map<std::string, size_t>& usageMap);

	// Load a mesh file (obj) into actors and return the first actor
	//  - return root-node actor (empty)
	API_EXPORT VzActor* LoadModelFile(const std::string& filename);

	API_EXPORT bool ExecutePluginFunction(const std::string& pluginFilename, const std::string& functionName, ParamMap<std::string>& io);

	API_EXPORT void PendingSubmitCommand(const bool pending);
	// Reload shaders
	API_EXPORT void ReloadShader();

	API_EXPORT void SetConfigure(const vzm::ParamMap<std::string>& configure, const std::string& section = "SHADER_ENGINE_SETTINGS");
}
