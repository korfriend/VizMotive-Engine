#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_COMP(COMP, RET) NameComponent* COMP = compfactory::GetNameComponent(componentVID_); \
	if (!COMP) {post(type_ + "(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}
	
	std::string VzBaseComp::GetName()
	{
		GET_COMP(comp, "");
		return comp->name;
	}
	void VzBaseComp::SetName(const std::string& name)
	{
		GET_COMP(comp, );
		comp->name = name;
		UpdateTimeStamp();
	}
}
