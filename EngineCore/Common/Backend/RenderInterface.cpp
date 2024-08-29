#include "RenderInterface.h"

using namespace vz::graphics;

namespace vz::renderer
{

	void AddDeferredMIPGen(const Texture& texture, bool preserve_coverage)
	{
		// call renderer 
	}
	void AddDeferredBlockCompression(const Texture& texture_src, const Texture& texture_bc)
	{
		// call renderer 
	}


	bool LoadShader(
		ShaderStage stage,
		Shader& shader,
		const std::string& filename,
		ShaderModel minshadermodel,
		const std::vector<std::string>& permutation_defines
	)
	{
		// call renderer 
		return true;
	}
}