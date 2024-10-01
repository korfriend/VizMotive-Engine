#pragma once
#include "Common/Backend/GBackendDevice.h"

namespace vz::graphics
{
	bool LoadShader(
		ShaderStage stage,
		Shader& shader,
		const std::string& filename,
		ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines
	);

	void LoadShaders()
	{

	}
}