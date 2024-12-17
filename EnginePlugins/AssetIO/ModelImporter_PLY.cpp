#include "AssetIO.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>

#include "Utils/Backlog.h"
#include "Utils/Helpers.h"

using namespace vz;

bool ImportModel_PLY(const std::string& fileName, vz::GeometryComponent* geometry)
{
	Assimp::Importer importer;

	const aiScene* scene = importer.ReadFile(fileName,
		aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);

	if (!scene || !scene->mRootNode || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
		std::cerr << "Error: " << importer.GetErrorString() << std::endl;
		return false;
	}

	using Primitive = GeometryComponent::Primitive;
	using SH = GeometryComponent::SH;

	std::vector<Primitive> parts(1);
	geometry->CopyPrimitivesFrom(parts);

	Primitive* mutable_primitive = geometry->GetMutablePrimitive(0);

	std::vector<XMFLOAT3>* vertex_positions = &mutable_primitive->GetMutableVtxPositions();
	std::vector<XMFLOAT3>* vertex_normals = &mutable_primitive->GetMutableVtxNormals();
	std::vector<uint32_t>* indices = &mutable_primitive->GetMutableIdxPrimives();

	std::vector<SH>* vertex_SHs = &mutable_primitive->GetMutableVtxSHs();
	std::vector<XMFLOAT4>* vertex_SOs = &mutable_primitive->GetMutableVtxScaleOpacities();
	std::vector<XMFLOAT4>* vertex_Qts = &mutable_primitive->GetMutableVtxQuaternions();

	// 메시에서 포인트 데이터 추출
	//for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
	//	aiMesh* mesh = scene->mMeshes[i];
	//
	//	std::cout << "Mesh " << i << " has " << mesh->mNumVertices << " vertices.\n";
	//
	//	std::vector<Point> points;
	//
	//	// 정점 데이터 읽기
	//	for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
	//		Point p;
	//		p.x = mesh->mVertices[j].x;
	//		p.y = mesh->mVertices[j].y;
	//		p.z = mesh->mVertices[j].z;
	//
	//		// 색상 데이터가 존재하면 읽기 (Optional)
	//		if (mesh->HasVertexColors(0)) {
	//			p.r = static_cast<unsigned char>(mesh->mColors[0][j].r * 255);
	//			p.g = static_cast<unsigned char>(mesh->mColors[0][j].g * 255);
	//			p.b = static_cast<unsigned char>(mesh->mColors[0][j].b * 255);
	//		}
	//		else {
	//			p.r = p.g = p.b = 255; // 기본 색상 (흰색)
	//		}
	//
	//		points.push_back(p);
	//	}
	//
	//	// 포인트 클라우드 출력
	//	std::cout << "Point Cloud Data:\n";
	//	for (const auto& p : points) {
	//		std::cout << "  Position: (" << p.x << ", " << p.y << ", " << p.z << ")";
	//		std::cout << " Color: (" << (int)p.r << ", " << (int)p.g << ", " << (int)p.b << ")\n";
	//	}
	//}

	return true;
}