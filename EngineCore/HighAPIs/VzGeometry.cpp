#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_GEO_COMP(COMP, RET) GeometryComponent* COMP = compfactory::GetGeometryComponent(componentVID_); \
	if (!COMP) {post(type_ + "(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzGeometry::MaskTestTriangle()
	{
		GET_GEO_COMP(geometry, );

		GeometryComponent::Primitive prim;

		std::vector<XMFLOAT3> vertexPositions(3);
		vertexPositions.push_back(XMFLOAT3(0, 1, 0));
		vertexPositions.push_back(XMFLOAT3(1, -0.3f, 0));
		vertexPositions.push_back(XMFLOAT3(-1, -0.3f, 0));

		std::vector<uint32_t> indexPrimitives = {0, 1, 2};

		prim.SetVtxPositions(vertexPositions, true);
		prim.SetIdxPrimives(indexPrimitives, true);
		
		geometry->MovePrimitive(prim, 0);
		UpdateTimeStamp();
	}
}