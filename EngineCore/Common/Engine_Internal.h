#pragma once
#include "Components/GComponents.h"

namespace vz
{
	namespace compfactory
	{
		size_t DestroyAll();
	}
}

namespace vzm
{
	std::recursive_mutex& GetEngineMutex();
}