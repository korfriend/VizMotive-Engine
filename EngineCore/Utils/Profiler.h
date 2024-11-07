#pragma once

#ifndef UTIL_EXPORT
#ifdef _WIN32
#define UTIL_EXPORT __declspec(dllexport)
#else
#define UTIL_EXPORT __attribute__((visibility("default")))
#endif
#endif

// QoL macros, allows writing just ScopedXxxProfiling without needing to declare a variable manually
#define ScopedCPUProfiling(name) vz::profiler::ScopedRangeCPU VZ_PROFILER_CONCAT(_vz_profiler_cpu_range,__LINE__)(name)
#define ScopedGPUProfiling(name, cmd) vz::profiler::ScopedRangeGPU VZ_PROFILER_CONCAT(_vz_profiler_gpu_range,__LINE__)(name, cmd)

// same as ScopedXxxProfiling, just will automatically use the function name as name, should only be used at the beginning of a function
#define ScopedCPUProfilingF ScopedCPUProfiling(__FUNCTION__)
#define ScopedGPUProfilingF(cmd) ScopedGPUProfiling(__FUNCTION__, cmd)

// internal helper macros to make somewhat unique variable names based on line numbers to prevent some compilers
// warning about shadowed variables
#define VZ_PROFILER_CONCAT(x,y) VZ_PROFILER_CONCAT_INDIRECT(x,y)
#define VZ_PROFILER_CONCAT_INDIRECT(x,y) x##y

namespace vz::graphics
{
	struct CommandList;
}
namespace vz::profiler
{
	typedef size_t range_id;

	// Begin collecting profiling data for the current frame
	UTIL_EXPORT void BeginFrame();

	// Finalize collecting profiling data for the current frame
	UTIL_EXPORT void EndFrame(graphics::CommandList* cmd);

	// Start a CPU profiling range
	UTIL_EXPORT range_id BeginRangeCPU(const char* name);

	// Start a GPU profiling range
	UTIL_EXPORT range_id BeginRangeGPU(const char* name, graphics::CommandList* cmd);

	// End a profiling range
	UTIL_EXPORT void EndRange(range_id id);

	// helper using RAII to avoid having to manually call BeginRangeCPU/EndRange at beginning/end
	struct ScopedRangeCPU
	{
		range_id id;
		inline ScopedRangeCPU(const char* name) { id = BeginRangeCPU(name); }
		inline ~ScopedRangeCPU() { EndRange(id); }
	};

	// same for BeginRangeGPU
	struct ScopedRangeGPU
	{
		range_id id;
		inline ScopedRangeGPU(const char* name, graphics::CommandList* cmd) { id = BeginRangeGPU(name, cmd); }
		inline ~ScopedRangeGPU() { EndRange(id); }
	};

	UTIL_EXPORT void DisableDrawForThisFrame();

	// Enable/disable profiling
	UTIL_EXPORT void SetEnabled(bool value);

	UTIL_EXPORT bool IsEnabled();
};

