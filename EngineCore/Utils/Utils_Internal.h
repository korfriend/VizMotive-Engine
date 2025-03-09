#pragma once
// the functions declared in this header file is visible only inside Core lib

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

		void Shutdown();
	}

	namespace backlog
	{
		void Destroy(); // Available only for Engine Manager
	}
}
