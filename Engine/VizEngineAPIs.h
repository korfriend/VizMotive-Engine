#pragma once
#include "VizComponentAPIs.h"

namespace vzm 
{
	// This must be called before using engine APIs
	//  - paired with DeinitEngineLib()
	__dojostatic VZRESULT InitEngineLib(const std::string& coreName = "VzmEngine", const std::string& logFileName = "EngineApi.log");
	// Get Entity ID 
	//  - return zero in case of failure 
	__dojostatic VID GetIdByName(const std::string& name);
	// Create new scene and return scene (NOT a scene item) ID, a scene 
	//  - return zero in case of failure (the name is already registered or overflow VID)
	__dojostatic VID NewScene(const std::string& sceneName);
	// Create new actor and return actor (scene item) ID (global entity)
	//  - Must belong to a scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID NewActor(const VID sceneId, const std::string& actorName, const ActorParams& aParams, const VID parentId = 0u);
	// Create new camera and return camera (scene item) ID (global entity), scene item
	//  - Must belong to a scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID NewCamera(const VID sceneId, const std::string& camName, const CameraParams& cParams, const VID parentId = 0u);
	// Create new camera and return light (scene item) ID (global entity), scene item
	//  - Must belong to a scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID NewLight(const VID sceneId, const std::string& lightName, const LightParams& lParams, const VID parentId = 0u);
	// Get CameraParams and return its pointer registered in renderer
	//  - return nullptr in case of failure 
	__dojostatic CameraParams* GetCamera(const VID camId);
	// Load model component and return resource ID (global entity), resource item
	//  - Must belong to the internal scene
	//  - return zero in case of failure (invalid sceneID, the name is already registered, or overflow VID)
	__dojostatic VID LoadMeshModel(const std::string& file, const std::string& rootName);
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

namespace vzm // helper APIs
{
	__dojoclass ArcBall
	{
	public:
		ArcBall();
		~ArcBall();
		// stage_center .. fvec3
		bool Intializer(const float stage_center[3], const float stage_radius);
		// pos_xy .. ivec2
		bool Start(const int pos_xy[2], const float screen_size[2],
			const float pos[3], const float view[3], const float up[3],
			const float np = 0.1f, const float fp = 100.f, const float sensitivity = 1.0f);
		// pos_xy .. ivec2
		// mat_r_onmove .. fmat4x4
		bool Move(const int pos_xy[2], float pos[3], float view[3], float up[3]);	// target is camera
		bool Move(const int pos_xy[2], float mat_r_onmove[16]);	// target is object
		bool PanMove(const int pos_xy[2], float pos[3], float view[3], float up[3]);
	};
}