#pragma once

#include "HighAPIs/VzEngineAPIs.h" // engine-level components

using namespace std;
using namespace vz;

__vmstatic bool GeneratePanoVolume(
	ParamMap<string>& ioComponents, // value is entity
	ParamMap<string>& ioParams,		// value is std::any (user-defined)
	ComponentManager& cm);