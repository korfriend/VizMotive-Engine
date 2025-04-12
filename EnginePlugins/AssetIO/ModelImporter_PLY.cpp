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
        vzlog_error("Invalid Entity(%llu)!", geometryEntity);
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

    // TODO: new implementation
    //  * previous trash code was deleted

    return true;
}
