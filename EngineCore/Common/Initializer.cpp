#include "Initializer.h"
#include "Version.h"
#include "Common/Backend/GRendererInterface.h"
#include "Utils/JobSystem.h"
#include "Utils/Timer.h"
#include "Utils/Backlog.h"

#include <string>
#include <thread>
#include <atomic>

namespace vz
{
	extern GraphicsPackage graphicsPackage;
}

namespace vz::initializer
{
	static std::atomic_bool initializationStarted{ false };
	static jobsystem::context ctx;
	static Timer timer;
	static std::atomic_bool systems[INITIALIZED_SYSTEM_COUNT]{};
	static uint32_t numMaxThreads = ~0u;
	void SetMaxThreadCount(uint32_t numThreads) { numMaxThreads = numThreads; }

	void InitializeComponentsImmediate()
	{
		if (IsInitializeFinished())
			return;
		if (!initializationStarted.load())
		{
			InitializeComponentsAsync();
		}
		WaitForInitializationsToFinish();
	}
	void InitializeComponentsAsync()
	{
		if (IsInitializeFinished())
			return;
		timer.record();

		initializationStarted.store(true);

		std::string ss;
		ss += "\n[initializer] Initializing Engine, please wait...\n";
		ss += "Version: ";
		ss += vz::version::GetVersionString();
		backlog::post(ss);

		// TODO : Shader dump system
		// 1. precompile project stores the compiled shaders to a header file (including binary array)
		// 2. if precompiled dump file (as a .h file), then, dummp process (refers to "renderer::GetShaderDumpCount();" in the reference code)
		//size_t shaderdump_count = renderer::GetShaderDumpCount();
		//if (shaderdump_count > 0)
		//{
		//	backlog::post("\nEmbedded shaders found: " + std::to_string(shaderdump_count));
		//}
		//else
		//{
		//	backlog::post("\nNo embedded shaders found, shaders will be compiled at runtime if needed.\n\tShader source path: " + renderer::GetShaderSourcePath() + "\n\tShader binary path: " + renderer::GetShaderPath());
		//}

		backlog::post("");
		jobsystem::Initialize(numMaxThreads);

		backlog::post("");

		jobsystem::Execute(ctx, [](jobsystem::JobArgs args) { graphicsPackage.pluginInitRenderer(); systems[INITIALIZED_SYSTEM_RENDERER].store(true); });
		
		// take a new thread and wait the above jobs (asynchronously)
		std::thread([] {
			jobsystem::Wait(ctx);
			backlog::post("\n[initializer] Engine Initialized (" + std::to_string((int)std::round(timer.elapsed())) + " ms)");
			}).detach();
	}

	bool IsInitializeFinished(INITIALIZED_SYSTEM system)
	{
		if (system == INITIALIZED_SYSTEM_COUNT)
		{
			return initializationStarted.load() && !jobsystem::IsBusy(ctx);
		}
		else
		{
			return systems[system].load();
		}
	}

	void WaitForInitializationsToFinish()
	{
		jobsystem::Wait(ctx);
	}
}