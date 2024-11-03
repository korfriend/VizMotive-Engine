#pragma once
#ifdef _WIN32
#define MESHIO_EXPORT __declspec(dllexport)
#else
#define MESHIO_EXPORT __attribute__((visibility("default")))
#endif

#include <string>
#include "Components/Components.h"

using Entity = uint32_t;

extern "C" MESHIO_EXPORT Entity ImportModel_OBJ(const std::string& fileName,
	std::vector<Entity>& actors,
	std::vector<Entity>& cameras, // obj does not include camera
	std::vector<Entity>& lights,
	std::vector<Entity>& geometries,
	std::vector<Entity>& materials,
	std::vector<Entity>& textures
);
extern "C" MESHIO_EXPORT bool ImportModel_GLTF(const std::string& fileName, vz::Scene& scene);
extern "C" MESHIO_EXPORT bool ExportModel_GLTF(const std::string& filename, vz::Scene& scene);
