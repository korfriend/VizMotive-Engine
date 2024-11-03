#pragma once
#ifdef _WIN32
#define MESHIO_EXPORT __declspec(dllexport)
#else
#define MESHIO_EXPORT __attribute__((visibility("default")))
#endif

#include <string>
#include "Components/Components.h"

extern "C" MESHIO_EXPORT bool ImportModel_OBJ(const std::string& fileName, vz::Scene& scene);
extern "C" MESHIO_EXPORT bool ImportModel_GLTF(const std::string& fileName, vz::Scene& scene);
extern "C" MESHIO_EXPORT bool ExportModel_GLTF(const std::string& filename, vz::Scene& scene);
