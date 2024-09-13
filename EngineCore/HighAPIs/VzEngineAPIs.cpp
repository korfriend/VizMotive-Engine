#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"
#include "Utils/Platform.h"

using namespace vz;

namespace vzm
{
	typedef bool(*GraphicsInitializer)();
	typedef vz::graphics::GraphicsDevice* (*GetGraphicsDevice)();
	typedef bool(*GraphicsDeinitializer)();
	GraphicsInitializer graphicsInitializer = nullptr;
	GraphicsDeinitializer graphicsDeinitializer = nullptr;
	GetGraphicsDevice graphicsGetDev = nullptr;

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


		std::string api = arguments.GetString("API", "DX12");
		if (api == "DX12")
		{
			//wi::renderer::SetShaderPath(wi::renderer::GetShaderPath() + "hlsl6/");
			graphicsInitializer = vz::platform::LoadModule<GraphicsInitializer>("RendererDX12", "Initialize");
			graphicsDeinitializer = vz::platform::LoadModule<GraphicsDeinitializer>("RendererDX12", "Deinitialize");
			graphicsGetDev = vz::platform::LoadModule<GetGraphicsDevice>("RendererDX12", "GetGraphicsDevice");

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
