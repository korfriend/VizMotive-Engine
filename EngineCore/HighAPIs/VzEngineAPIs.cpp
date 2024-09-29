#include "VzEngineAPIs.h"
#include "Components/GComponents.h"
#include "Utils/Backlog.h"
#include "Utils/Platform.h"
#include "Common/Backend/GBackendDevice.h"
#include "Common/RenderPath3D.h"

using namespace vz;

GEngineConfig gEngine;

namespace vzm
{
	typedef bool(*PI_GraphicsInitializer)();
	typedef vz::graphics::GraphicsDevice* (*PI_GetGraphicsDevice)();
	typedef bool(*PI_GraphicsDeinitializer)();
	PI_GraphicsInitializer graphicsInitializer = nullptr;
	PI_GraphicsDeinitializer graphicsDeinitializer = nullptr;
	PI_GetGraphicsDevice graphicsGetDev = nullptr;

#define CHECK_API_VALIDITY(RET) if (!initialized) { backlog::post("High-level API is not initialized!!", backlog::LogLevel::Error); return RET; }

	bool initialized = false;
	vz::graphics::GraphicsDevice* graphicsDevice = nullptr;

	namespace vzcomp
	{
		std::unordered_map<SceneVID, std::unique_ptr<VzScene>> scenes;
		std::unordered_map<RendererVID, std::unique_ptr<VzRenderer>> renderers;
		std::unordered_map<CamVID, std::unique_ptr<VzCamera>> cameras;
		std::unordered_map<VID, std::unique_ptr<VzSceneComp>> sceneComponents;
		std::unordered_map<VID, std::unique_ptr<VzResource>> resources;
	}
}

namespace vzm
{
	VZRESULT InitEngineLib(const vzm::ParamMap<std::string>& arguments)
	{
		if (initialized)
		{
			backlog::post("Already initialized!", backlog::LogLevel::Warn);
			return VZ_WARNNING;
		}

		// assume DX12 rendering engine
		gEngine.api = arguments.GetString("API", "DX12");
		if (gEngine.api == "DX12")
		{
			//wi::renderer::SetShaderPath(wi::renderer::GetShaderPath() + "hlsl6/");
			graphicsInitializer = vz::platform::LoadModule<PI_GraphicsInitializer>("RendererDX12", "Initialize");
			graphicsDeinitializer = vz::platform::LoadModule<PI_GraphicsDeinitializer>("RendererDX12", "Deinitialize");
			graphicsGetDev = vz::platform::LoadModule<PI_GetGraphicsDevice>("RendererDX12", "GetGraphicsDevice");
		}

		// initialize the graphics backend
		graphicsInitializer();

		// graphics device
		graphicsDevice = graphicsGetDev();

		return VZ_OK;
	}

	VzScene* NewScene(const std::string& sceneName)
	{
		Scene* scene = Scene::CreateScene(sceneName);
		SceneVID vid = scene->GetEntity();
		auto it = vzcomp::scenes.emplace(vid, std::make_unique<VzScene>(vid, "vzm::NewScene"));
		compfactory::CreateNameComponent(vid, sceneName);
		return it.first->second.get();
	}

	VzRenderer* NewRenderer(const std::string& rendererName)
	{
		RenderPath3D* renderer = canvas::CreateRenderPath3D(graphicsDevice, rendererName);
		RendererVID vid = renderer->GetEntity();
		auto it = vzcomp::renderers.emplace(vid, std::make_unique<VzRenderer>(vid, "vzm::NewRenderer"));
		compfactory::CreateNameComponent(vid, rendererName);
		return it.first->second.get();
	}

	VzSceneComp* NewSceneComponent(const SCENE_COMPONENT_TYPE compType, const std::string& compName, const VID parentVid)
	{
		// add more interfaces ... for default definition
		// transform component
		// name component
		// camera component
		return nullptr;
	}

	VZRESULT DeinitEngineLib()
	{
		CHECK_API_VALIDITY(VZ_FAIL);

		graphicsDeinitializer();
		return VZ_OK;
	}
}
