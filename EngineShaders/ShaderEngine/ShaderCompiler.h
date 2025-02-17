#pragma once
#include "GBackend/GBackend.h"

#include <string>
#include <vector>

namespace vz::shadercompiler
{
	using namespace vz::graphics;

	enum class Flags
	{
		NONE = 0,
		DISABLE_OPTIMIZATION = 1 << 0,
		STRIP_REFLECTION = 1 << 1,
	};
	struct CompilerInput
	{
		Flags flags = Flags::NONE;
		graphics::ShaderFormat format = graphics::ShaderFormat::NONE;
		graphics::ShaderStage stage = graphics::ShaderStage::Count;
		// if the shader relies on a higher shader model feature, it must be declared here.
		//	But the compiler can also choose a higher one internally, if needed
		graphics::ShaderModel minshadermodel = graphics::ShaderModel::SM_5_0;
		std::string shadersourcefilename;
		std::string entrypoint = "main";
		std::vector<std::string> include_directories;
		std::vector<std::string> defines;
	};
	struct CompilerOutput
	{
		std::shared_ptr<void> internal_state;
		inline bool IsValid() const { return internal_state.get() != nullptr; }

		const uint8_t* shaderdata = nullptr;
		size_t shadersize = 0;
		std::vector<uint8_t> shaderhash;
		std::string error_message;
		std::vector<std::string> dependencies;
	};
	void Compile(const CompilerInput& input, CompilerOutput& output);

	bool SaveShaderAndMetadata(const std::string& shaderfilename, const CompilerOutput& output);
	bool IsShaderOutdated(const std::string& shaderfilename);
	void SetRecentHeaderTimeStamp(const uint64_t timestamp);

	void RegisterShader(const std::string& shaderfilename);
	size_t GetRegisteredShaderCount();
	bool CheckRegisteredShadersOutdated();
}

template<>
struct enable_bitmask_operators<vz::shadercompiler::Flags> {
	static const bool enable = true;
};
