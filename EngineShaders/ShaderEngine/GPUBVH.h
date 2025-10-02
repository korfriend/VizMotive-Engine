#pragma once
#include "GBackend/GBackendDevice.h"
#include "GBackend/GShaderInterface.h"
#include "Components/GComponents.h"
#include "../Shaders/ShaderInterop.h"

namespace vz::gpubvh
{
	using Entity = uint64_t;

	bool UpdateGeometryGPUBVH(GGeometryComponent* geometry, graphics::CommandList cmd);

	// this is called during the rendering process (after UpdateRenderData that involves FrameCB)
	bool UpdateSceneGPUBVH(GScene* scene, graphics::CommandList cmd);

	void Initialize();
	void Deinitialize();
};
