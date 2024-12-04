#include "Archive.h"
#include "Utils/Helpers.h"
#include "Utils/Helpers2.h"
#include "Utils/Backlog.h"
#include "Utils/ECS.h"

#include "ThirdParty/stb_image.h"


namespace vz
{
	static std::unordered_map<Entity, std::unique_ptr<Archive>> archives;

	Archive* Archive::GetArchive(const Entity entity) {
		auto it = archives.find(entity);
		return it != archives.end() ? it->second.get() : nullptr;
	}
	Archive* Archive::GetFirstArchiveByName(const std::string& name) {
		for (auto& it : archives) {
			if (it.second->GetArchiveName() == name) return it.second.get();
		}
		return nullptr;
	}
	Archive* Archive::CreateArchive(const std::string& name, const Entity entity)
	{
		Entity ett = entity;
		if (entity == 0)
		{
			ett = ecs::CreateEntity();
		}

		archives[ett] = std::make_unique<Archive>(ett, name);
		return archives[ett].get();
	}
	bool Archive::DestroyArchive(const Entity entity)
	{
		auto it = archives.find(entity);
		if (it == archives.end())
		{
			backlog::post("Archive::DestroyArchive >> Invalid Entity! (" + std::to_string(entity) + ")", backlog::LogLevel::Error);
			return false;
		}
		it->second.reset();
		archives.erase(it);
		return true;
	}

	void Archive::DestroyAll()
	{
		archives.clear();
	}
}

namespace vz
{
	// this should always be only INCREMENTED and only if a new serialization is implemeted somewhere!
	static constexpr uint64_t __archiveVersion = 0;
	// this is the version number of which below the archive is not compatible with the current version
	static constexpr uint64_t __archiveVersionBarrier = 0;

	// version history is logged in ArchiveVersionHistory.txt file!

	//Archive::Archive()
	//{
	//	CreateEmpty();
	//}
	Archive::Archive(const std::string& fileName, bool readMode)
		: readMode(readMode), fileName(fileName)
	{
		if (!fileName.empty())
		{
			directory = vz::helper::GetDirectoryFromPath(fileName);
			if (readMode)
			{
				if (vz::helper::FileRead(fileName, DATA))
				{
					data_ptr = DATA.data();
					data_ptr_size = DATA.size();
					SetReadModeAndResetPos(true);
				}
			}
			else
			{
				CreateEmpty();
			}
		}
	}

	Archive::Archive(const uint8_t* data, size_t size)
	{
		data_ptr = data;
		data_ptr_size = size;
		SetReadModeAndResetPos(true);
	}

	void Archive::CreateEmpty()
	{
		version = __archiveVersion;
		DATA.resize(128); // starting size
		data_ptr = DATA.data();
		data_ptr_size = DATA.size();
		SetReadModeAndResetPos(false);
	}

	void Archive::SetReadModeAndResetPos(bool isReadMode)
	{
		readMode = isReadMode;
		pos = 0;

		if (readMode)
		{
			(*this) >> version;
			if (version < __archiveVersionBarrier)
			{
				backlog::post("File is not supported!\nReason: The archive version (" + std::to_string(version) + ") is no longer supported! This is likely because trying to open a file that was created by a version of Wicked Engine that is too old.", backlog::LogLevel::Error);
				Close();
				return;
			}
			if (version > __archiveVersion)
			{
				backlog::post("File is not supported!\nReason: The archive version (" + std::to_string(version) + ") is higher than the program's (" + std::to_string(__archiveVersion) + ")!\nThis is likely due to trying to open an Archive file that was not created by Wicked Engine.", backlog::LogLevel::Error);
				Close();
				return;
			}

			(*this) >> thumbnail_data_size;
			thumbnail_data_ptr = data_ptr + pos;
			pos += thumbnail_data_size;
		}
		else
		{
			(*this) << version;
			(*this) << thumbnail_data_size;
			const uint8_t* thumbnail_data_dst = data_ptr + pos;
			for (size_t i = 0; i < thumbnail_data_size; ++i)
			{
				(*this) << thumbnail_data_ptr[i];
			}
			thumbnail_data_ptr = thumbnail_data_dst;
		}
	}

	void Archive::Close()
	{
		if (!readMode && !fileName.empty())
		{
			SaveFile(fileName);
		}
		DATA.clear();
	}

	bool Archive::SaveFile(const std::string& fileName)
	{
		return vz::helper::FileWrite(fileName, data_ptr, pos);
	}

	bool Archive::SaveHeaderFile(const std::string& fileName, const std::string& dataName)
	{
		return vz::helper::Bin2H(data_ptr, pos, fileName, dataName.c_str());
	}

	const std::string& Archive::GetSourceDirectory() const
	{
		return directory;
	}

	const std::string& Archive::GetSourceFileName() const
	{
		return fileName;
	}

	bool createTexture(
		graphics::Texture& texture,
		const void* data,
		uint32_t width,
		uint32_t height,
		graphics::Format format = vz::graphics::Format::R8G8B8A8_UNORM,
		graphics::Swizzle swizzle = {}
	)
	{
		if (data == nullptr)
		{
			return false;
		}
		graphics::GraphicsDevice* device = vz::graphics::GetDevice();

		graphics::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.mip_levels = 1;
		desc.array_size = 1;
		desc.format = format;
		desc.sample_count = 1;
		desc.bind_flags = graphics::BindFlag::SHADER_RESOURCE;
		desc.swizzle = swizzle;

		graphics::SubresourceData InitData;
		InitData.data_ptr = data;
		InitData.row_pitch = width * GetFormatStride(format) / GetFormatBlockSize(format);

		return device->CreateTexture(&desc, &InitData, &texture);
	}

	vz::graphics::Texture Archive::CreateThumbnailTexture() const
	{
		if (thumbnail_data_size == 0)
			return {};
		int width = 0;
		int height = 0;
		int channels = 0;
		uint8_t* rgba = stbi_load_from_memory(thumbnail_data_ptr, (int)thumbnail_data_size, &width, &height, &channels, 4);
		if (rgba == nullptr)
			return {};
		vz::graphics::Texture texture;
		createTexture(texture, rgba, (uint32_t)width, (uint32_t)height);
		stbi_image_free(rgba);
		return texture;
	}

	void Archive::SetThumbnailAndResetPos(const vz::graphics::Texture& texture)
	{
		std::vector<uint8_t> thumbnail_data;
		vz::helper2::saveTextureToMemoryFile(texture, "JPG", thumbnail_data);
		thumbnail_data_size = thumbnail_data.size();
		thumbnail_data_ptr = thumbnail_data.data();
		SetReadModeAndResetPos(false); // start over in write mode with thumbnail image data
	}

	vz::graphics::Texture Archive::PeekThumbnail(const std::string& filename)
	{
		std::vector<uint8_t> filedata;

		size_t required_size = sizeof(uint64_t) * 2; // version and thumbnail data size

		vz::helper::FileRead(filename, filedata, required_size); // read only up to version and thumbnail data size
		if (filedata.empty())
			return {};

		{
			vz::Archive archive(filedata.data(), filedata.size());
			if (archive.IsOpen())
			{
				if (archive.thumbnail_data_size == 0)
					return {};
				required_size += archive.thumbnail_data_size;
			}
		}

		vz::helper::FileRead(filename, filedata, required_size); // read up to end of thumbnail data
		if (filedata.empty())
			return {};

		vz::Archive archive(filedata.data(), filedata.size());
		return archive.CreateThumbnailTexture();
	}

}
