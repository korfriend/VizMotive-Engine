#pragma once
#include "GBackend/GBackendDevice.h"

#include <string>

namespace vz::helper2
{
	bool CompressPNG(const uint8_t* src_data, size_t src_size, std::vector<uint8_t>& dst_data);
	bool DecompressPNG(const uint8_t* src_data, size_t src_size, std::vector<uint8_t>& dst_data);
	bool Compress(const uint8_t* src_data, size_t src_size, std::vector<uint8_t>& dst_data, int level);
	bool Decompress(const uint8_t* src_data, size_t src_size, std::vector<uint8_t>& dst_data);

	// Returns file path if successful, empty string otherwise
	std::string screenshot(const vz::graphics::SwapChain& swapchain, const std::string& name = "");

	// Save raw pixel data from the texture to memory
	bool saveTextureToMemory(const vz::graphics::Texture& texture, std::vector<uint8_t>& texturedata);

	// Save texture to memory as a file format
	bool saveTextureToMemoryFile(const vz::graphics::Texture& texture, const std::string& fileExtension, std::vector<uint8_t>& filedata);

	// Save raw texture data to memory as file format
	bool saveTextureToMemoryFile(const std::vector<uint8_t>& textureData, const vz::graphics::TextureDesc& desc, const std::string& fileExtension, std::vector<uint8_t>& filedata);

	// Save texture to file format
	bool saveTextureToFile(const vz::graphics::Texture& texture, const std::string& fileName);

	// Save raw texture data to file format
	bool saveTextureToFile(const std::vector<uint8_t>& texturedata, const vz::graphics::TextureDesc& desc, const std::string& fileName);
};
