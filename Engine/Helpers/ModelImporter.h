#pragma once
#include <string>
#include "vzEngine.h"

void ImportModel_OBJ(const std::string& fileName, vz::scene::Scene& scene);
void ImportModel_GLTF(const std::string& fileName, vz::scene::Scene& scene);
void ExportModel_GLTF(const std::string& filename, vz::scene::Scene& scene);
