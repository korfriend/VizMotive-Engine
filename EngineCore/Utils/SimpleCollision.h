#pragma once
#include "Utils/Geometrics.h"

using Entity = uint64_t;

namespace vz::bvhcollision
{
	bool CollisionPairwiseCheck(const Entity geometryEntity1, const Entity transformEntity1, const Entity geometryEntity2, const Entity transformEntity2,
		int& partIndex1, int& triIndex1, int& partIndex2, int& triIndex2);
}

