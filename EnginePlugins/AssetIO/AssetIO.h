#pragma once

#include "Components/Components.h"
#include <string>

extern "C" PLUGIN_EXPORT Entity ImportModel_OBJ(const std::string& fileName);

// just for a single geometry
extern "C" PLUGIN_EXPORT bool ImportModel_STL(const std::string& fileName, const Entity geometryEntity);
extern "C" PLUGIN_EXPORT bool ImportModel_PLY(const std::string& fileName, const Entity geometryEntity);

extern "C" PLUGIN_EXPORT bool ImportModel_GLTF(const std::string& fileName, const Entity sceneEntity);
extern "C" PLUGIN_EXPORT bool ExportModel_GLTF(const std::string& filename, const Entity sceneEntity);
