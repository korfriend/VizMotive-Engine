#pragma once
#include <string>
#include "vzEngine.h"

using namespace vz::ecs;

Entity ImportModel_OBJ(const std::string& fileName, vz::scene::Scene& scene);
void ImportModel_GLTF(const std::string& fileName, vz::scene::Scene& scene);
void ExportModel_GLTF(const std::string& filename, vz::scene::Scene& scene);
