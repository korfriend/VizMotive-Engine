#pragma once
#include "GBackend/GBackendDevice.h"
#include "../Shaders/ShaderInterop.h"

namespace vz::gpubvh
{
	using Entity = uint64_t;

	bool UpdateGeometryGPUBVH(const Entity geometryEntity, graphics::CommandList cmd);

	// this is called during the rendering process (after UpdateRenderData that involves FrameCB)
	bool UpdateSceneGPUBVH(const Entity sceneEntity);

	void Initialize();
	void Deinitialize();
};
