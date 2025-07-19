#include "Version.h"

#include <string>
#include <assert.h>

namespace vz::version
{
	// main engine core
	const int major = 2;
	// minor features, major updates, breaking compatibility changes
	const int minor = 0;
	// minor bug fixes, alterations, refactors, updates
	const int revision = 139;
	
	const std::string versionString = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(revision) + ".preview";

	long GetVersion()
	{
		assert(major < 1000 && minor < 1000 && revision < 10000);
		return major * 10000000 + minor * 10000 + revision;
	}

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
		return versionString.c_str();
	}

	const char* GetCreditsString()
	{
		static const char* credits = R"(
Credits
-----------
Created by DongJoon Kim

Contributors:
---------------------------
^^,

Supporters
---------------------------
OsstemImplant Co.,Ltd.
		)";

		return credits;
	}

}
