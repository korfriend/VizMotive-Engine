#pragma once
#include "GBackend/GBackendDevice.h"
#include "../Shaders/ShaderInterop.h"

namespace vz::gpubvh
{
	using Entity = uint32_t;

	bool UpdateGeometryGPUBVH(const Entity geometryEntity, graphics::CommandList cmd);

	// this is called during the rendering process (after UpdateRenderData that involves FrameCB)
	bool UpdateSceneGPUBVH(const ShaderScene& sceneShader, graphics::CommandList cmd);

	void Initialize();
	void Deinitialize();
};
