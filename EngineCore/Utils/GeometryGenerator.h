#pragma once
#include "vzMath.h"
#include <vector>

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
	UTIL_EXPORT bool GenerateIcosahedronGeometry(Entity geometryEntity, const float radius = 1.f, const uint32_t detail = 0u);
	UTIL_EXPORT bool GenerateTorusKnotGeometry(Entity geometryEntity, const float radius = 1.f, const float tube = 0.4f,
		const uint32_t tubularSegments = 64, const uint32_t radialSegments = 8, const uint32_t p = 2, const uint32_t q = 3);
	UTIL_EXPORT bool GenerateBoxGeometry(Entity geometryEntity, const float width = 1.f, const float height = 1.f, const float depth = 1.f,
		const uint32_t widthSegments = 1u, const uint32_t heightSegments = 1u, const uint32_t depthSegments = 1u);
	UTIL_EXPORT bool GenerateSphereGeometry(Entity geometryEntity, const float radius = 1.f, const uint32_t widthSegments = 32u, const uint32_t heightSegments = 16u, 
		const float phiStart = 0.f, const float phiLength = 0.f, const float thetaStart = 0.f, const float thetaLength = 0.f);
	UTIL_EXPORT bool GenerateCapsuleGeometry(Entity geometryEntity, const float radius = 1.f, const float length = 1.f, const uint32_t capSegments = 4u, const uint32_t radialSegments = 8u);
	UTIL_EXPORT bool GenerateTubeGeometry(Entity geometryEntity, const std::vector<XMFLOAT3>& path, uint32_t tubularSegments = 64u, const float radius = 1.f, uint32_t radialSegments = 8u, const bool closed = true);
}