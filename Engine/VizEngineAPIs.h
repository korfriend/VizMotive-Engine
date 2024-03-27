#pragma once
#include "VizComponentAPIs.h"

namespace vzm 
{
	// This must be called before using engine APIs
	//  - paired with DeinitEngineLib()
	__dojostatic VZRESULT InitEngineLib(const std::string& coreName = "VzmEngine", const std::string& logFileName = "EngineApi.log");

	// Create new scene and return scene (NOT a scene item) ID, a scene 
	//  - return zero in case of failure (the name is already registered or overflow VID)
	__dojostatic VID NewScene(const std::string& sceneName);
	// Create new actor and return actor (scene item) ID (global entity)
	//  - Must belong to a scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID NewActor(const VID sceneId, const std::string& actorName, const ActorParameter& aParams, const VID parentId = 0u);
	// Create new camera and return camera (scene item) ID (global entity), scene item
	//  - Must belong to a scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID NewCamera(const VID sceneId, const std::string& camName, const CameraParameter& cParams, const VID parentId = 0u);
	// Create new camera and return light (scene item) ID (global entity), scene item
	//  - Must belong to a scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID NewLight(const VID sceneId, const std::string& lightName, const LightParameter& lParams, const VID parentId = 0u);
	// Load model component and return resource ID (global entity), resource item
	//  - Must belong to the internal scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID LoadMeshModel(const std::string& file, const std::string& resName);
	// Render a scene (sceneId) with a camera (camId)
	//  - Must belong to the internal scene
	__dojostatic VZRESULT Render(const int camId);
	__dojostatic VZRESULT UpdateScene(const int sceneId); // animation or simulation...
	// Get a graphics render target view 
	//  - Must belong to the internal scene
	__dojostatic void* GetGraphicsSharedRenderTarget(const int camId, const void* graphicsDev2, uint32_t* w = NULL, uint32_t* h = NULL);
	__dojostatic void* TEST();

	__dojostatic VZRESULT DeinitEngineLib();
}
