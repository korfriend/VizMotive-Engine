#include "AssetIO.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>

#include "Utils/Backlog.h"
#include "Utils/Helpers.h"

using namespace vz;
bool ImportModel_PLY(const std::string& fileName, vz::GeometryComponent* geometry)
{
	std::ifstream file(fileName, std::ios::binary);
	if (!file.is_open())
	{
		backlog::post("Error opening PLY file: " + fileName, backlog::LogLevel::Error);
		return false;
	}

	std::string line;
	bool headerEnded = false;
	int vertexCount = 0;

	// 1. Read PLY header
	while (std::getline(file, line))
	{
		if (line.find("element vertex") != std::string::npos)
		{
			vertexCount = std::stoi(line.substr(line.find_last_of(" ") + 1));
			std::cerr << "vertexCount : " << vertexCount << std::endl;
		}
		else if (line == "end_header")
		{
			headerEnded = true;
			break;
		}
	}

	if (!headerEnded || vertexCount == 0)
	{
		backlog::post("Error: Invalid PLY header.", backlog::LogLevel::Error);
		return false;
	}

	//vertexCount = 100;

	// 2. Prepare GeometryComponent
	using Primitive = GeometryComponent::Primitive;
	using SH = GeometryComponent::SH;

	std::vector<Primitive> parts(1);
	geometry->CopyPrimitivesFrom(parts);

	Primitive* mutable_primitive = geometry->GetMutablePrimitive(0);

	std::vector<XMFLOAT3>* vertex_positions = &mutable_primitive->GetMutableVtxPositions();
	std::vector<XMFLOAT3>* vertex_normals = &mutable_primitive->GetMutableVtxNormals();
	std::vector<SH>* vertex_SHs = &mutable_primitive->GetMutableVtxSHs();
	std::vector<XMFLOAT4>* vertex_SOs = &mutable_primitive->GetMutableVtxScaleOpacities();
	std::vector<XMFLOAT4>* vertex_Qts = &mutable_primitive->GetMutableVtxQuaternions();

	std::vector<uint32_t>* indices = &mutable_primitive->GetMutableIdxPrimives();

	vertex_positions->reserve(vertexCount);
	vertex_normals->reserve(vertexCount);
	vertex_SHs->reserve(vertexCount);
	vertex_SOs->reserve(vertexCount);
	vertex_Qts->reserve(vertexCount);
	indices->reserve(vertexCount);

	// 3. Read binary data
	for (int i = 0; i < vertexCount; i++)
	{
		float x, y, z, nx, ny, nz, opacity;
		float scale[3], rot[4];
		float sh_coeffs[48]; // 48 floats for SH coefficients (16 XMFLOAT3 = 48 floats)

		// Read vertex position and normal
		file.read(reinterpret_cast<char*>(&x), sizeof(float));
		file.read(reinterpret_cast<char*>(&y), sizeof(float));
		file.read(reinterpret_cast<char*>(&z), sizeof(float));

		file.read(reinterpret_cast<char*>(&nx), sizeof(float));
		file.read(reinterpret_cast<char*>(&ny), sizeof(float));
		file.read(reinterpret_cast<char*>(&nz), sizeof(float));

		indices->push_back(i);

		// Read 48 SH coefficients
		for (int j = 0; j < 48; j++)
		{
			file.read(reinterpret_cast<char*>(&sh_coeffs[j]), sizeof(float));
		}

		// Read opacity, scale, rotation
		file.read(reinterpret_cast<char*>(&opacity), sizeof(float));
		file.read(reinterpret_cast<char*>(scale), sizeof(float) * 3);
		file.read(reinterpret_cast<char*>(rot), sizeof(float) * 4);

		// 4. Store data
		vertex_positions->emplace_back(x, y, z);
		vertex_normals->emplace_back(nx, ny, nz);

		SH sh;
		std::memcpy(sh.dcSHs, sh_coeffs, sizeof(float) * 48);
		vertex_SHs->emplace_back(sh);

		vertex_SOs->emplace_back(scale[0], scale[1], scale[2], opacity);
		vertex_Qts->emplace_back(rot[0], rot[1], rot[2], rot[3]);
	}

	geometry->UpdateRenderData();

	file.close();

	// std::cerr
	//{
	//	const auto &positions = mutable_primitive->GetVtxPositions();
	//	const auto &normals = mutable_primitive->GetVtxNormals();

	//	std::cerr << "Loaded " << positions.size() << " vertices from " << fileName << std::endl;
	//	for (size_t i = 0; i < positions.size(); ++i)
	//	{
	//		const auto &p = positions[i];
	//		const auto &n = normals[i];
	//		std::cerr << "Vertex " << i << ": Pos(" << p.x << ", " << p.y << ", " << p.z << "), "
	//				  << "Normal(" << n.x << ", " << n.y << ", " << n.z << ")\n";
	//	}
	//}

	return true;
}