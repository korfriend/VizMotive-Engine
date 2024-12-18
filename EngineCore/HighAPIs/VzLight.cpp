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


	void VzLight::SetLightType(const LightType type)
	{
		GET_LIGHT_COMP(light, );
		light->SetLightType((LightComponent::LightType)type);
	}
	void VzLight::SetSpotlightConeAngle(const float outerConeAngle, const float innerConeAngle)
	{
		GET_LIGHT_COMP(light, );
		light->SetInnerConeAngle(innerConeAngle);
		light->SetOuterConeAngle(outerConeAngle);
	}
	void VzLight::SetPointlightLength(const float length)
	{
		GET_LIGHT_COMP(light, );
		light->SetLength(length);
	}
	void VzLight::SetRange(const float range)
	{
		GET_LIGHT_COMP(light, );
		light->SetRange(range);
	}
	void VzLight::SetColor(const vfloat3 color)
	{
		GET_LIGHT_COMP(light, );
		light->SetLightColor(*(XMFLOAT3*)&color);
	}

	void VzLight::SetIntensity(const float intensity)
	{
		GET_LIGHT_COMP(light, );
		light->SetLightIntensity(intensity);
	}
}