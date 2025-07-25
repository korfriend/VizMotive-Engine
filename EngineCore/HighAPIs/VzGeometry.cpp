#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"
#include "Utils/Platform.h"
#include "Utils/Helpers.h"
#include "GBackend/GModuleLoader.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_GEO_COMP(COMP, RET) GeometryComponent* COMP = compfactory::GetGeometryComponent(componentVID_); \
	if (!COMP) {post("GeometryComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzGeometry::MakeTestTriangle()
	{
		GET_GEO_COMP(geometry, );

		float x_offsets[3] = {-4.f, 0, 4.f};
		for (size_t i = 0; i < 3; ++i)
		{
			GeometryComponent::Primitive prim;

			std::vector<XMFLOAT3> vertexPositions;
			vertexPositions.reserve(3);
			vertexPositions.push_back(XMFLOAT3(x_offsets[i] + 0, 1, 0));
			vertexPositions.push_back(XMFLOAT3(x_offsets[i] + 1, -0.3f, 0));
			vertexPositions.push_back(XMFLOAT3(x_offsets[i] + -1, -0.3f, 0));

			std::vector<XMFLOAT3> normals;
			normals.reserve(3);
			normals.push_back(XMFLOAT3(0, 0, -1));
			normals.push_back(XMFLOAT3(0, 0, -1));
			normals.push_back(XMFLOAT3(0, 0, -1));

			std::vector<uint32_t> indexPrimitives = { 0, 1, 2 };

			prim.SetVtxPositions(vertexPositions, true);
			prim.SetVtxNormals(normals, true);
			prim.SetIdxPrimives(indexPrimitives, true);

			geometrics::AABB aabb;
			aabb._min = XMFLOAT3(-1.f, -1.f, -1.f);
			aabb._max = XMFLOAT3(1.f, 1.f, 1.f);

			prim.SetAABB(aabb);
			prim.SetPrimitiveType(GeometryComponent::PrimitiveType::TRIANGLES);

			geometry->AddMovePrimitiveFrom(std::move(prim));
		}
		geometry->UpdateRenderData();
		UpdateTimeStamp();
	}

	void VzGeometry::MakeTestQuadWithUVs()
	{
		GET_GEO_COMP(geometry, );

		GeometryComponent::Primitive prim;

		std::vector<XMFLOAT3> vertexPositions;
		vertexPositions.reserve(4);
		vertexPositions.push_back(XMFLOAT3(-1, 1, 0));
		vertexPositions.push_back(XMFLOAT3( 1, 1, 0));
		vertexPositions.push_back(XMFLOAT3( 1,-1, 0));
		vertexPositions.push_back(XMFLOAT3(-1,-1, 0));

		std::vector<XMFLOAT3> normals;
		normals.reserve(4);
		normals.push_back(XMFLOAT3(0, 0, 1));
		normals.push_back(XMFLOAT3(0, 0, 1));
		normals.push_back(XMFLOAT3(0, 0, 1));
		normals.push_back(XMFLOAT3(0, 0, 1));

		std::vector<XMFLOAT2> uvs0;
		uvs0.reserve(4);
		uvs0.push_back(XMFLOAT2(0, 0));
		uvs0.push_back(XMFLOAT2(1, 0));
		uvs0.push_back(XMFLOAT2(1, 1));
		uvs0.push_back(XMFLOAT2(0, 1));

		std::vector<uint32_t> indexPrimitives = { 0, 3, 1, 1, 3, 2 };

		prim.SetVtxPositions(vertexPositions, true);
		prim.SetVtxNormals(normals, true);
		prim.SetVtxUVSet0(uvs0, true);
		prim.SetIdxPrimives(indexPrimitives, true);

		geometrics::AABB aabb;
		aabb._min = XMFLOAT3(-1.f, -1.f, -1.f);
		aabb._max = XMFLOAT3(1.f, 1.f, 1.f);

		prim.SetAABB(aabb);
		prim.SetPrimitiveType(GeometryComponent::PrimitiveType::TRIANGLES);

		geometry->MovePrimitiveFrom(std::move(prim), 0);
		geometry->UpdateRenderData();
		UpdateTimeStamp();
	}

	enum ModelFormat
	{
		STL = 0,
		PLY
	};
	bool VzGeometry::LoadGeometryFile(const std::string& filename)
	{
		std::string ext = helper::toUpper(helper::GetExtensionFromFileName(filename));
		if (ext != "STL" && ext != "PLY" && ext != "SPLAT" && ext != "KSPLAT")
		{
			backlog::post("LoadGeometryFile dose not support " + ext + " file!", backlog::LogLevel::Error);
			return false;
		}

		typedef Entity(*PI_Function)(const std::string& fileName, const Entity geometryEntity);

		PI_Function lpdll_function = nullptr;
		if (ext == "STL")
		{
			lpdll_function = platform::LoadModule<PI_Function>("AssetIO", "ImportModel_STL", importedModules);
		}
		else if (ext == "PLY")
		{
			lpdll_function = platform::LoadModule<PI_Function>("AssetIO", "ImportModel_PLY", importedModules);
		}
		else if (ext == "SPLAT" || ext == "KSPLAT")
		{
			lpdll_function = platform::LoadModule<PI_Function>("AssetIO", "ImportModel_SPLAT", importedModules);
		}
		else
		{
			assert(0);
		}

		if (lpdll_function == nullptr)
		{
			backlog::post("vzm::LoadModelFile >> Invalid plugin function!", backlog::LogLevel::Error);
			return false;
		}
		GET_GEO_COMP(geometry, false);
		if (lpdll_function(filename, componentVID_))
		{
			return true;
		}
		UpdateTimeStamp();
		return false;
	}

	size_t VzGeometry::GetNumParts() const
	{
		GET_GEO_COMP(geometry, 0);
		return geometry->GetNumParts();
	}

	bool VzGeometry::IsGPUBVHEnabled() const
	{
		GET_GEO_COMP(geometry, false);
		return geometry->IsGPUBVHEnabled();
	}
	void VzGeometry::EnableGPUBVH(const bool enabled)
	{
		GET_GEO_COMP(geometry, );
		geometry->SetGPUBVHEnabled(enabled);
	}

	size_t VzGeometry::GetMemoryUsageCPU() const
	{
		GET_GEO_COMP(geometry, 0);
		return geometry->GetMemoryUsageCPU();
	}

	void VzGeometry::GetAABB(vfloat3& posMin, vfloat3& posMax) const
	{
		GET_GEO_COMP(geometry, );
		geometrics::AABB aabb = geometry->GetAABB();
		posMin = *(vfloat3*)&aabb._min;
		posMax = *(vfloat3*)&aabb._max;
	}
}