#pragma once
#include "CommonInclude.h"
#include "vzGraphicsDevice.h"
#include "vzScene_Decl.h"

namespace vz
{
	struct GPUBVH
	{
		// Scene BVH intersection resources:
		vz::graphics::GPUBuffer bvhNodeBuffer;
		vz::graphics::GPUBuffer bvhParentBuffer;
		vz::graphics::GPUBuffer bvhFlagBuffer;
		vz::graphics::GPUBuffer primitiveCounterBuffer;
		vz::graphics::GPUBuffer primitiveIDBuffer;
		vz::graphics::GPUBuffer primitiveBuffer;
		vz::graphics::GPUBuffer primitiveMortonBuffer;
		uint32_t primitiveCapacity = 0;
		bool IsValid() const { return primitiveCounterBuffer.IsValid(); }

		void Update(const vz::scene::Scene& scene);
		void Build(const vz::scene::Scene& scene, vz::graphics::CommandList cmd) const;

		void Clear();

		static void Initialize();
	};
}
