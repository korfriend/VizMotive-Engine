#pragma once
namespace vz::graphics
{
	struct CommandList;
}
namespace vz
{
	namespace profiler
	{
		// Begin collecting profiling data for the current frame
		void BeginFrame();

		// Finalize collecting profiling data for the current frame
		void EndFrame(graphics::CommandList* cmd);
	}
}
