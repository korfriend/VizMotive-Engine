#pragma once
#include "GBackend/GBackendDevice.h"
#include "../Shaders/ShaderInterop.h"

namespace vz::gsplat
{
	using Entity = uint64_t;

	void UpdateGaussianSplatting(const Entity geometryEntity, graphics::CommandList cmd);
	
	// TODO
	// private ...
	void updateCapacityGaussians(const Entity geometryEntity, uint32_t capacityGaussians);

	void Initialize();
	void Deinitialize();
};
