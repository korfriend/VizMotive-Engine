#pragma once
#include "Components.h" // engine-level components
#include "ComponentManager.h" // engine-level components Manager

using namespace std;
using namespace vzm;

__vmstatic bool GeneratePanoVolume(
	ParamMap<string>& ioComponents, // value is entity
	ParamMap<string>& ioParams,		// value is std::any (user-defined)
	ComponentManager& cm);