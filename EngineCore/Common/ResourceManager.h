#pragma once
#include "CommonInclude.h"
#include "Backend/GBackendDevice.h"
#include "Components/Components.h"

#include <memory>
#include <string>
#include <unordered_set>

namespace vz::resourcemanager
{
	enum class Mode
	{
		NO_EMBEDDING,		// default behaviour, serialization will not embed resource file datas
		EMBED_FILE_DATA,	// serialization will embed file datas if possible
	};
	void SetMode(Mode param);
	Mode GetMode();
	std::vector<std::string> GetSupportedImageExtensions();
	std::vector<std::string> GetSupportedSoundExtensions();
	std::vector<std::string> GetSupportedVideoExtensions();
	std::vector<std::string> GetSupportedScriptExtensions();
	std::vector<std::string> GetSupportedFontStyleExtensions();

	// Order of these must not change as the flags can be serialized!
	enum class Flags
	{
		NONE = 0,
		IMPORT_COLORGRADINGLUT = 1 << 0, // image import will convert resource to 3D color grading LUT
		IMPORT_RETAIN_FILEDATA = 1 << 1, // file data of the resource will be kept in memory, you will be able to use Resource::GetFileData()
		IMPORT_NORMALMAP = 1 << 2, // image import will try to use optimal normal map encoding
		IMPORT_BLOCK_COMPRESSED = 1 << 3, // image import will request block compression for uncompressed or transcodable formats
		IMPORT_DELAY = 1 << 4, // delay importing resource until later, for example when proper flags can be determined.
		STREAMING = 1 << 5, // use streaming if possible
	};

	// Load a resource
	//	name : file name of resource
	//	flags : specify flags that modify behaviour (optional)
	//	filedata : pointer to file data, if file was loaded manually (optional)
	//	filesize : size of file data, if file was loaded manually (optional)
	//	container_filename : if name is not the name of source file, set the source file name here
	//	container_fileoffset : if using container_filename, you can give the offset for the resource within the file here
	Resource Load(
		const std::string& name,
		Flags flags = Flags::NONE,
		const uint8_t* filedata = nullptr,
		size_t filesize = ~0ull,
		const std::string& container_filename = "",
		size_t container_fileoffset = 0
	);
	// Check if a resource is currently loaded
	bool Contains(const std::string& name);
	// Invalidate all resources
	void Clear();

	// Set threshold relative to memory budget for streaming
	//	If memory usage is below threshold, streaming will work regularly
	//	If memory usage is above threshold, streaming will try to reduce usage
	void SetStreamingMemoryThreshold(float value);
	float GetStreamingMemoryThreshold();

	// Update all streaming resources, call it once per frame on the main thread
	//	Launching or finalizing background streaming jobs is attempted here
	void UpdateStreamingResources(float dt);

	// Returns true if any of the loaded resources are outdated compared to their files
	bool CheckResourcesOutdated();

	// Reload all resources that are outdated
	void ReloadOutdatedResources();

	struct ResourceSerializer
	{
		std::vector<Resource> resources;
	};

	void Serialize_READ(vz::Archive& archive, ResourceSerializer& resources);
	void Serialize_WRITE(vz::Archive& archive, const std::unordered_set<std::string>& resource_names);

}

template<>
struct enable_bitmask_operators<vz::resourcemanager::Flags> {
	static const bool enable = true;
};
