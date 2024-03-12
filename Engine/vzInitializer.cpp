#include "vzInitializer.h"
#include "vzEngine.h"

#include <string>
#include <thread>
#include <atomic>

namespace vz::initializer
{
	static bool initializationStarted = false;
	static vz::jobsystem::context ctx;
	static vz::Timer timer;
	static std::atomic_bool systems[INITIALIZED_SYSTEM_COUNT]{};

	void InitializeComponentsImmediate()
	{
		InitializeComponentsAsync();
		vz::jobsystem::Wait(ctx);
	}
	void InitializeComponentsAsync()
	{
		timer.record();

		initializationStarted = true;

		std::string ss;
		ss += "\n[vz::initializer] Initializing Wicked Engine, please wait...\n";
		ss += "Version: ";
		ss += vz::version::GetVersionString();
		vz::backlog::post(ss);

		size_t shaderdump_count = vz::renderer::GetShaderDumpCount();
		if (shaderdump_count > 0)
		{
			vz::backlog::post("\nEmbedded shaders found: " + std::to_string(shaderdump_count));
		}
		else
		{
			vz::backlog::post("\nNo embedded shaders found, shaders will be compiled at runtime if needed.\n\tShader source path: " + vz::renderer::GetShaderSourcePath() + "\n\tShader binary path: " + vz::renderer::GetShaderPath());
		}

		vz::backlog::post("");
		vz::jobsystem::Initialize();

		vz::backlog::post("");
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::font::Initialize(); systems[INITIALIZED_SYSTEM_FONT].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::image::Initialize(); systems[INITIALIZED_SYSTEM_IMAGE].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::input::Initialize(); systems[INITIALIZED_SYSTEM_INPUT].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::renderer::Initialize(); systems[INITIALIZED_SYSTEM_RENDERER].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::texturehelper::Initialize(); systems[INITIALIZED_SYSTEM_TEXTUREHELPER].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::EmittedParticleSystem::Initialize(); systems[INITIALIZED_SYSTEM_EMITTEDPARTICLESYSTEM].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::Ocean::Initialize(); systems[INITIALIZED_SYSTEM_OCEAN].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::gpusortlib::Initialize(); systems[INITIALIZED_SYSTEM_GPUSORTLIB].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::GPUBVH::Initialize(); systems[INITIALIZED_SYSTEM_GPUBVH].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::physics::Initialize(); systems[INITIALIZED_SYSTEM_PHYSICS].store(true); });
		vz::jobsystem::Execute(ctx, [](vz::jobsystem::JobArgs args) { vz::audio::Initialize(); systems[INITIALIZED_SYSTEM_AUDIO].store(true); });

		// Initialize this immediately:
		//vz::lua::Initialize(); systems[INITIALIZED_SYSTEM_LUA].store(true);

		std::thread([] {
			vz::jobsystem::Wait(ctx);
			vz::backlog::post("\n[vz::initializer] Wicked Engine Initialized (" + std::to_string((int)std::round(timer.elapsed())) + " ms)");
		}).detach();

	}

	bool IsInitializeFinished(INITIALIZED_SYSTEM system)
	{
		if (system == INITIALIZED_SYSTEM_COUNT)
		{
			return initializationStarted && !vz::jobsystem::IsBusy(ctx);
		}
		else
		{
			return systems[system].load();
		}
	}
}
