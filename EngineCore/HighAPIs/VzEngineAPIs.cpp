#include "VzEngineAPIs.h"
#include "Components/GComponents.h"
#include "Utils/Backlog.h"
#include "Utils/Platform.h"
#include "Common/Backend/GBackendDevice.h"

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

	VZRESULT DeinitEngineLib()
	{
		CHECK_API_VALIDITY(VZ_FAIL);

		graphicsDeinitializer();
		return VZ_OK;
	}
}
