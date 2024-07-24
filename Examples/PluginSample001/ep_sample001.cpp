#include "ep_sample001.h"

__vmstatic bool GeneratePanoVolume(
	ParamMap<string>& ioComponents, // value is entity
	ParamMap<string>& ioParams,		// value is std::any (user-defined)
	ComponentManager& cm)
{
	Entity ett = ioComponents.GetParam("VolActor1");
	// transform, volume texture, geometry, MI otf,....
	Transform& tr = cm.GetComponent<Transform>(ett);
}