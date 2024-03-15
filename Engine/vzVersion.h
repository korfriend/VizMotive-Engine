#ifndef VZMENGINE_VERSION_DEFINED
#define VZMENGINE_VERSION_DEFINED

namespace vz::version
{
	long GetVersion();
	// major features
	int GetMajor();
	// minor features, major bug fixes
	int GetMinor();
	// minor bug fixes, alterations
	int GetRevision();
	const char* GetVersionString();

	const char* GetCreditsString();
}

#endif // VZMENGINE_VERSION_DEFINED
