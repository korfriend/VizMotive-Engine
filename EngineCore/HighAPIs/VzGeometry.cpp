#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

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

		GeometryComponent::Primitive prim;

		std::vector<XMFLOAT3> vertexPositions;
		vertexPositions.reserve(3);
		vertexPositions.push_back(XMFLOAT3(0, 1, 0));
		vertexPositions.push_back(XMFLOAT3(1, -0.3f, 0));
		vertexPositions.push_back(XMFLOAT3(-1, -0.3f, 0));

		std::vector<uint32_t> indexPrimitives = {0, 1, 2};

		prim.SetVtxPositions(vertexPositions, true);
		prim.SetIdxPrimives(indexPrimitives, true);
	
		geometrics::AABB aabb;
		aabb._min = XMFLOAT3(-1.f, -1.f, -1.f);
		aabb._max = XMFLOAT3( 1.f,  1.f,  1.f);

		prim.SetAABB(aabb);
		prim.SetPrimitiveType(GeometryComponent::PrimitiveType::TRIANGLES);

		geometry->MovePrimitiveFrom(prim, 0);
		//geometry->UpdateRenderData();
		UpdateTimeStamp();
	}
}