#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"
#include "Utils/Platform.h"

using namespace vz;

namespace vzm
{
	vz::graphics::GraphicsDevice* graphicsDevice = nullptr;
	VZRESULT InitEngineLib(const vzm::ParamMap<std::string>& arguments)
	{
		static bool initialized = false;
		if (initialized)
		{
			backlog::post("Already initialized!", backlog::LogLevel::Warn);
			return VZ_WARNNING;
		}

		// assume DX12 rendering engine
		typedef void(*GraphicsInitializer)();
		typedef vz::graphics::GraphicsDevice* (*GetGraphicsDevice)();

		GraphicsInitializer graphics_initializer = vz::platform::LoadModule<GraphicsInitializer>("RendererDX12", "Initialize");
		graphics_initializer();
		GetGraphicsDevice graphics_get_dev = vz::platform::LoadModule<GetGraphicsDevice>("RendererDX12", "GetGraphicsDevice");
		graphicsDevice = graphics_get_dev();

		//sceneManager.Initialize(arguments);

		return VZ_OK;
	}

	VZRESULT DeinitEngineLib()
	{
		return VZ_OK;
	}
}
