#pragma once

#include <string>
#include "Components/Components.h"

enum NodeType
{
	ACTOR,
	CAMERA,
	LIGHT,
	MATERIAL,
	GEOMETRY,
	TEXTURE
};
Entity CreateNode(
	std::vector<Entity>& actors,
	std::vector<Entity>& cameras, // obj does not include camera
	std::vector<Entity>& lights,
	std::vector<Entity>& geometries,
	std::vector<Entity>& materials,
	std::vector<Entity>& textures,
	const NodeType ntype, const std::string& name, Entity parent_entity = 0u);

#define ALLOC_NODES actors, cameras, lights, geometries, materials, textures

extern "C" PLUGIN_EXPORT Entity ImportModel_OBJ(const std::string& fileName,
	std::vector<Entity>& actors,
	std::vector<Entity>& cameras, // obj does not include camera
	std::vector<Entity>& lights,
	std::vector<Entity>& geometries,
	std::vector<Entity>& materials,
	std::vector<Entity>& textures
);

// just for a single geometry
extern "C" PLUGIN_EXPORT bool ImportModel_STL(const std::string& fileName, vz::GeometryComponent* geometry);

extern "C" PLUGIN_EXPORT bool ImportModel_GLTF(const std::string& fileName, vz::Scene& scene);
extern "C" PLUGIN_EXPORT bool ExportModel_GLTF(const std::string& filename, vz::Scene& scene);
