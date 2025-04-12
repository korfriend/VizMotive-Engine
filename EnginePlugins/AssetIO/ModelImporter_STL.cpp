#include "AssetIO.h"
#include "Utils/Backlog.h"
#include "Utils/Helpers.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>

using namespace vz;

bool ImportModel_STL(const std::string& fileName, const Entity geometryEntity)
{
	if (!compfactory::ContainGeometryComponent(geometryEntity))
	{
		vzlog_error("Invalid Entity(%llu)!", geometryEntity);
		return false;
	}
	Assimp::Importer importer;

	// dummy
	//std::vector<Entity> dummy;

	// import STL 
	const aiScene* scene = importer.ReadFile(fileName, aiProcess_Triangulate | aiProcess_GenNormals);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		backlog::post("Error loading STL file: " + std::string(importer.GetErrorString()), backlog::LogLevel::Error);
		return false;
	}

	if (scene->mNumMeshes == 0)
	{
		backlog::post("STL file has NO triangle", backlog::LogLevel::Error);
		return false;
	}

	//std::string name = helper::GetFileNameFromPath(fileName);
	//Entity entity = CreateNode(dummy, dummy, dummy, geometries, dummy, dummy, GEOMETRY, name);
	//GeometryComponent* geometry = compfactory::GetGeometryComponent(entity);

	using Primitive = GeometryComponent::Primitive;
	std::vector<Primitive> parts(scene->mNumMeshes);

	for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
		const aiMesh* mesh = scene->mMeshes[i];

		Primitive* mutable_primitive = &parts[i];
		mutable_primitive->SetPrimitiveType(GeometryComponent::PrimitiveType::TRIANGLES);
		//PrintMeshInfo(mesh);

		std::vector<XMFLOAT3>* vertex_positions = &mutable_primitive->GetMutableVtxPositions();
		std::vector<XMFLOAT3>* vertex_normals = &mutable_primitive->GetMutableVtxNormals();
		std::vector<uint32_t>* indices = &mutable_primitive->GetMutableIdxPrimives();

		vertex_positions->reserve(mesh->mNumVertices);
		vertex_normals->reserve(mesh->mNumVertices);
		indices->reserve(mesh->mNumFaces * 3);

		if (mesh->HasFaces()) {
			//std::cout << "Number of Faces: " << mesh->mNumFaces << std::endl;
			for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
				const aiFace& face = mesh->mFaces[i];
				//std::cout << "Face " << i << " has " << face.mNumIndices << " indices: ";
				for (unsigned int j = 0; j < face.mNumIndices; j++) {
					//std::cout << face.mIndices[j] << " ";
					indices->push_back(face.mIndices[j]);
				}
				//std::cout << std::endl;
			}
		}

		if (mesh->HasNormals()) {
			//std::cout << "Vertex Normals: " << std::endl;
			for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
				//std::cout << "Vertex " << i << ": ("
				//	<< mesh->mNormals[i].x << ", "
				//	<< mesh->mNormals[i].y << ", "
				//	<< mesh->mNormals[i].z << ")" << std::endl;

				vertex_positions->push_back(*(XMFLOAT3*)&mesh->mVertices[i]);
				vertex_normals->push_back(*(XMFLOAT3*)&mesh->mNormals[i]);
			}
		}
	}

	vz::GeometryComponent* geometry = compfactory::GetGeometryComponent(geometryEntity);
	geometry->MovePrimitivesFrom(std::move(parts));
	geometry->UpdateRenderData();
	// thread safe!
	//compfactory::EntitySafeExecute([&parts](const std::vector<Entity>& entities) {
	//	vz::GeometryComponent* geometry = compfactory::GetGeometryComponent(entities[0]);
	//	geometry->MovePrimitivesFrom(std::move(parts));
	//	}, { geometryEntity });
	return true;
}