#pragma once
#include "VzClasses.h"

namespace vzm
{
	// This must be called before using engine APIs
	//  - paired with DeinitEngineLib()
	extern "C" API_EXPORT VZRESULT InitEngineLib(const vzm::ParamMap<std::string>& arguments = vzm::ParamMap<std::string>());
	extern "C" API_EXPORT VZRESULT DeinitEngineLib();
}
