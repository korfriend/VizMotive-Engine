#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzGeometry : VzResource
	{
		VzGeometry(const VID vid, const std::string& originFrom)
			: VzResource(vid, originFrom, COMPONENT_TYPE::GEOMETRY) {}

		void MakeTestTriangle();
		void MakeTestQuadWithUVs();
		
		// STL
		bool LoadGeometryFile(const std::string& filename);

		size_t GetNumParts() const;

		bool IsGPUBVHEnabled() const;
		void EnableGPUBVH(const bool enabled);
		
		size_t GetMemoryUsageCPU() const;

		void GetAABB(vfloat3& posMin, vfloat3& posMax) const;
	};
}
