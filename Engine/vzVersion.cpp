#include "vzVersion.h"

#include <string>

namespace vz::version
{
	// note this project mainly refers to Wicked Engine
	// we need to follow up wicked engine version
	
	// main engine core
	const int major = 0;
	// minor features, major updates, breaking compatibility changes
	const int minor = 1;	// from 71 
	// minor bug fixes, alterations, refactors, updates
	const int revision = 1;	// from 389

	const std::string version_string = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(revision);

	int GetMajor()
	{
		return major;
	}
	int GetMinor()
	{
		return minor;
	}
	int GetRevision()
	{
		return revision;
	}
	const char* GetVersionString()
	{
		return version_string.c_str();
	}

	const char* GetCreditsString()
	{
		static const char* credits = R"(
Credits
-----------
Created by Dongjoon Kim

This project is originated from Wicked Engine https://github.com/turanszkij/WickedEngine
)";

		return credits;
	}

}
