#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_LIGHT_COMP(COMP, RET) LightComponent* COMP = compfactory::GetLightComponent(componentVID_); \
	if (!COMP) {post("LightComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzLight::SetLightIntensity(const float intensity)
	{
		GET_LIGHT_COMP(light, );
		light->SetLightIntensity(intensity);
	}
}