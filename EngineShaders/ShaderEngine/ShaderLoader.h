#pragma once
#include "GBackend/GBackendDevice.h"
#include "Components/GComponents.h"

namespace vz
{
}

namespace vz::shader
{
	using namespace graphics;

	bool LoadShader(
		ShaderStage stage,
		Shader& shader,
		const std::string& filename,
		ShaderModel minshadermodel = graphics::ShaderModel::SM_6_0,
		const std::vector<std::string>& permutation_defines = {}
	);

	void LoadShaders();
}