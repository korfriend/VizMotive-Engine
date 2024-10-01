#include "Version.h"

#include <string>

namespace vz::version
{
	// main engine core
	const int major = 2;
	// minor features, major updates, breaking compatibility changes
	const int minor = 0;
	// minor bug fixes, alterations, refactors, updates
	const int revision = 0;

	const std::string versionString = std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(revision);

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
