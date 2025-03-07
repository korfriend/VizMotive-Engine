#pragma once
#include "CommonInclude.h"

#ifndef UTIL_EXPORT
#ifdef _WIN32
#define UTIL_EXPORT __declspec(dllexport)
#else
#define UTIL_EXPORT __attribute__((visibility("default")))
#endif
#endif

using Entity = uint32_t;

namespace vz::geogen
{
	UTIL_EXPORT bool GenerateIcosahedronGeometry(Entity geometryEntity, const float radius, const uint32_t detail);
	UTIL_EXPORT bool GenerateTorusKnotGeometry(Entity geometryEntity, const float radius = 1.f, const float tube = 0.4f,
		const uint32_t tubularSegments = 64, const uint32_t radialSegments = 8, const uint32_t p = 2, const uint32_t q = 3);
}