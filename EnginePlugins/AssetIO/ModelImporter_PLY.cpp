#include "AssetIO.h"
#include "Utils/Backlog.h"
#include "Utils/Helpers.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstring>

using namespace vz;
bool ImportModel_PLY(const std::string& fileName, const Entity geometryEntity)
{
    vz::GeometryComponent* geometry = compfactory::GetGeometryComponent(geometryEntity);
    if (geometry == nullptr)
    {
        vzlog_error("Invalid Entity(%d)!", geometryEntity);
        return false;
    }
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

    // 2. Prepare GeometryComponent
    using Primitive = GeometryComponent::Primitive;
    //using SH = GeometryComponent::SH;

    std::vector<Primitive> parts(1);
    geometry->CopyPrimitivesFrom(parts);

    Primitive* mutable_primitive = geometry->GetMutablePrimitive(0);
    mutable_primitive->SetPrimitiveType(GeometryComponent::PrimitiveType::POINTS);
    /*
    std::vector<XMFLOAT3>* vertex_positions = &mutable_primitive->GetMutableVtxPositions();
    std::vector<XMFLOAT3>* vertex_normals = &mutable_primitive->GetMutableVtxNormals();
    std::vector<float>* vertex_SHs = &mutable_primitive->GetMutableVtxSHs();
    std::vector<XMFLOAT4>* vertex_SOs = &mutable_primitive->GetMutableVtxScaleOpacities();
    std::vector<XMFLOAT4>* vertex_Qts = &mutable_primitive->GetMutableVtxQuaternions();

    std::vector<uint32_t>* indices = &mutable_primitive->GetMutableIdxPrimives();

    vertex_positions->reserve(vertexCount);
    vertex_normals->reserve(vertexCount);
    vertex_SOs->reserve(vertexCount);
    vertex_Qts->reserve(vertexCount);
    indices->reserve(vertexCount);

	vertex_SHs->resize(vertexCount * mutable_primitive->shLevel * 3);

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

        // scale exponential
        scale[0] = std::expf(scale[0]);
        scale[1] = std::expf(scale[1]);
        scale[2] = std::expf(scale[2]);

        // opacity sigmoid
        opacity = 1.0f / (1.0f + std::expf(-opacity));

        // Quaternion Normalize
        float rot_length = std::sqrt(rot[0] * rot[0] + rot[1] * rot[1] + rot[2] * rot[2] + rot[3] * rot[3]);
        rot[0] /= rot_length;
        rot[1] /= rot_length;
        rot[2] /= rot_length;
        rot[3] /= rot_length;

        // 4. Store data
        vertex_positions->emplace_back(x, y, z);
        vertex_normals->emplace_back(nx, ny, nz);

        //SH sh;
        std::memcpy(&vertex_SHs->at(i * 16 * 3), sh_coeffs, sizeof(float) * 16 * 3);
        //vertex_SHs->emplace_back(sh);

        vertex_SOs->emplace_back(scale[0], scale[1], scale[2], opacity);
        vertex_Qts->emplace_back(rot[0], rot[1], rot[2], rot[3]);
    }

	file.close();

	geometry->UpdateRenderData();
	//((GGeometryComponent*)geometry)->UpdateRenderDataGaussianSplatting();


    
    const auto& positions = mutable_primitive->GetVtxPositions();
    const auto& vertex_SOs_ref = mutable_primitive->GetMutableVtxScaleOpacities();
    const auto& vertex_Qts_ref = mutable_primitive->GetMutableVtxQuaternions();
    const auto& vertex_SHs_ref = mutable_primitive->GetMutableVtxSHs();

    // Determine the limit for printing
    size_t limit = std::min<size_t>(1000, positions.size());

    std::cerr << "Loaded " << positions.size() << " vertices." << std::endl;
    for (size_t i = 10; i < limit; ++i) {
        const auto& p = positions[i];
        const auto& scale = vertex_SOs_ref[i];
        const auto& rotation = vertex_Qts_ref[i];
        const auto& shs = vertex_SHs_ref[i];

        std::cerr << "Vertex : " << i << " Scale(" << scale.x << ", " << scale.y << ", " << scale.z << "), alpha : " << scale.w << std::endl;

        //std::cerr << "Vertex " << i << ": Pos("
        //    << p.x << ", " << p.y << ", " << p.z << "), "
        //    << "Scale(" << scale.x << ", " << scale.y << ", " << scale.z << "), "
        //    << "Rotation(" << rotation.x << ", " << rotation.y << ", "
        //    << rotation.z << ", " << rotation.w << ")" << std::endl;

        //std::cerr << "SH Coefficients:" << std::endl;

        //for (int k = 0; k < 16; ++k) {
        //    std::cerr << "("
        //        << shs.dcSHs[k].x << ", "
        //        << shs.dcSHs[k].y << ", "
        //        << shs.dcSHs[k].z << ") " << '\n';
        //}
        std::cerr << std::endl;
    }


    /**/

    return true;
}
