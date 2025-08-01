#include "GComponents.h"
#include "Common/ResourceManager.h"
#include "Utils/Helpers.h"
#include "Utils/Backlog.h"
#include "Utils/JobSystem.h"
#include <thread>

namespace vz
{
	void GEnvironmentComponent::LoadSkyMap(const std::string& fileName)
	{ 
		skyMapName_ = fileName; 
		skyMap = resourcemanager::Load(skyMapName_);
		timeStampSetter_ = TimerNow; 
	}
	void GEnvironmentComponent::LoadColorGradingMap(const std::string& fileName)
	{ 
		colorGradingMapName_ = fileName;
		colorGradingMap = resourcemanager::Load(skyMapName_);
		timeStampSetter_ = TimerNow; 
	}
	void GEnvironmentComponent::LoadVolumetricCloudsWeatherMapFirst(const std::string& fileName)
	{ 
		volumetricCloudsWeatherMapFirstName_ = fileName;
		volumetricCloudsWeatherMapFirst = resourcemanager::Load(volumetricCloudsWeatherMapFirstName_);
		timeStampSetter_ = TimerNow; 
	}
	void GEnvironmentComponent::LoadVolumetricCloudsWeatherMapSecond(const std::string& fileName)
	{ 
		volumetricCloudsWeatherMapSecondName_ = fileName;
		volumetricCloudsWeatherMapSecond = resourcemanager::Load(volumetricCloudsWeatherMapSecondName_);
		timeStampSetter_ = TimerNow; 
	}
}