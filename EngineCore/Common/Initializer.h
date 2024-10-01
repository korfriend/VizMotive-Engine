#pragma once

namespace vz::initializer
{
	enum INITIALIZED_SYSTEM
	{
		INITIALIZED_SYSTEM_RENDERER,
		INITIALIZED_SYSTEM_TEXTUREHELPER,
		INITIALIZED_SYSTEM_GPUSORTLIB,
		INITIALIZED_SYSTEM_GPUBVH,

		INITIALIZED_SYSTEM_COUNT
	};

	// Initializes systems and blocks CPU until it is complete
	void InitializeComponentsImmediate();
	// Begins initializing systems, but doesn't block CPU. Check completion status with IsInitializeFinished()
	void InitializeComponentsAsync();
	// Check if systems have been initialized or not
	//	system : specify to check a specific system, or leave default to check all systems
	bool IsInitializeFinished(INITIALIZED_SYSTEM system = INITIALIZED_SYSTEM_COUNT);
	// Wait for all system initializations to finish
	void WaitForInitializationsToFinish();
}
