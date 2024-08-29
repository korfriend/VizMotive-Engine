#pragma once
#include "CommonInclude.h"
#include "GBackendDevice.h"

#include <memory>
#include <limits>

namespace vz::renderer
{
	// Add a texture that should be mipmapped whenever it is feasible to do so
	void AddDeferredMIPGen(const vz::graphics::Texture& texture, bool preserve_coverage = false);
	void AddDeferredBlockCompression(const vz::graphics::Texture& texture_src, const vz::graphics::Texture& texture_bc);

	bool LoadShader(
		vz::graphics::ShaderStage stage,
		vz::graphics::Shader& shader,
		const std::string& filename,
		vz::graphics::ShaderModel minshadermodel = vz::graphics::ShaderModel::SM_6_0,
		const std::vector<std::string>& permutation_defines = {}
	);
};

