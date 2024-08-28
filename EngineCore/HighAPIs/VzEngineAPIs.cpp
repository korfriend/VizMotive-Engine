#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

using namespace vz;

namespace vzm
{
	VZRESULT InitEngineLib(const vzm::ParamMap<std::string>& arguments)
	{
		static bool initialized = false;
		if (initialized)
		{
			backlog::post("Already initialized!", backlog::LogLevel::Warn);
			return VZ_WARNNING;
		}

		//sceneManager.Initialize(arguments);

		return VZ_OK;
	}

	VZRESULT DeinitEngineLib()
	{
		return VZ_OK;
	}
}
