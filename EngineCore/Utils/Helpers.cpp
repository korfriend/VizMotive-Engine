#include "Helpers.h"

#include <filesystem>

namespace vz::helper
{
	std::string GetTempDirectoryPath()
	{
		auto path = std::filesystem::temp_directory_path();
		return path.generic_u8string();
	}

	std::string GetCacheDirectoryPath()
	{
#ifdef PLATFORM_LINUX
		const char* xdg_cache = std::getenv("XDG_CACHE_HOME");
		if (xdg_cache == nullptr || *xdg_cache == '\0') {
			const char* home = std::getenv("HOME");
			if (home != nullptr) {
				return std::string(home) + "/.cache";
			}
			else {
				// shouldn't happen, just to be safe
				return GetTempDirectoryPath();
			}
		}
		else {
			return xdg_cache;
		}
#else
		return GetTempDirectoryPath();
#endif
	}

	std::string GetCurrentPath()
	{
		auto path = std::filesystem::current_path();
		return path.generic_u8string();
	}
}