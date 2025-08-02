#include "GComponents.h"
#include "Common/ResourceManager.h"
#include "Utils/Helpers.h"
#include "Utils/Backlog.h"
#include "Utils/JobSystem.h"
#include <thread>

namespace vz
{
	bool GEnvironmentComponent::LoadSkyMap(const std::string& fileName)
	{ 
		skyMapName_ = fileName; 
		skyMap = resourcemanager::Load(skyMapName_);
		timeStampSetter_ = TimerNow; 
		return skyMap.IsValid();
	}
	bool GEnvironmentComponent::LoadColorGradingMap(const std::string& fileName)
	{ 
		colorGradingMapName_ = fileName;
		colorGradingMap = resourcemanager::Load(skyMapName_);
		timeStampSetter_ = TimerNow;
		return colorGradingMap.IsValid();
	}
	bool GEnvironmentComponent::LoadVolumetricCloudsWeatherMapFirst(const std::string& fileName)
	{ 
		volumetricCloudsWeatherMapFirstName_ = fileName;
		volumetricCloudsWeatherMapFirst = resourcemanager::Load(volumetricCloudsWeatherMapFirstName_);
		timeStampSetter_ = TimerNow;
		return volumetricCloudsWeatherMapFirst.IsValid();
	}
	bool GEnvironmentComponent::LoadVolumetricCloudsWeatherMapSecond(const std::string& fileName)
	{ 
		volumetricCloudsWeatherMapSecondName_ = fileName;
		volumetricCloudsWeatherMapSecond = resourcemanager::Load(volumetricCloudsWeatherMapSecondName_);
		timeStampSetter_ = TimerNow;
		return volumetricCloudsWeatherMapSecond.IsValid();
	}
}

namespace vz
{
	bool GProbeComponent::LoadTexture(const std::string& fileName)
	{
		textureName_ = fileName;
		resource = resourcemanager::Load(textureName_);
		texture = resource.GetTexture();
		timeStampSetter_ = TimerNow;
		return texture.IsValid();
	}

	void GProbeComponent::RemoveTexture()
	{
		textureName_ = {};
		resource = {};
		texture = {};
		timeStampSetter_ = TimerNow;
	}

	size_t ProbeComponent::GetMemorySizeInBytes() const
	{
		return graphics::ComputeTextureMemorySizeInBytes(((GProbeComponent*)this)->texture.desc);
	}
}