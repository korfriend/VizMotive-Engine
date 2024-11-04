#pragma once

#include <string>
#include "Components/Components.h"

extern "C" PLUGIN_EXPORT Entity ImportModel_OBJ(const std::string& fileName,
	std::vector<Entity>& actors,
	std::vector<Entity>& cameras, // obj does not include camera
	std::vector<Entity>& lights,
	std::vector<Entity>& geometries,
	std::vector<Entity>& materials,
	std::vector<Entity>& textures
);
extern "C" PLUGIN_EXPORT bool ImportModel_GLTF(const std::string& fileName, vz::Scene& scene);
extern "C" PLUGIN_EXPORT bool ExportModel_GLTF(const std::string& filename, vz::Scene& scene);
