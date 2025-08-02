#pragma once
#include "Utils/vzMath.h"
#include "Utils/Color.h"
#include "GBackend/GBackend.h"

#include <string>

using Entity = uint64_t;

namespace vz
{
	using VUID = uint64_t;
	inline constexpr VUID INVALID_VUID = 0;

	// This is a data container used for serialization purposes.
	//	It can be used to READ or WRITE data, but not both at the same time.
	//	An archive that was created in WRITE mode can be changed to read mode and vica-versa
	//	The data flow is always FIFO (first in, first out)
	class Archive
	{
	public:
		static Archive* GetArchive(const Entity entity);
		static Archive* GetFirstArchiveByName(const std::string& name);
		static Archive* CreateArchive(const std::string& name, const Entity entity = 0);
		static bool DestroyArchive(const Entity entity);
		static void DestroyAll();
		static bool StoreSerializedResources(const std::string& sourceDir);

	public:
		struct Header
		{
			uint64_t version = 0;
			union Properties
			{
				struct
				{
					uint64_t thumbnail_data_size : 32;
					uint64_t compressed : 1;
					uint64_t reserved : 31;
				} bits;
				uint64_t raw = 0;
			} properties;
		};
		static_assert(sizeof(Header) == sizeof(uint64_t) * 2);

	private:
		Header header;
		bool readMode = false; // archive can be either read or write mode, but not both
		size_t pos = 0; // position of the next memory operation, relative to the data's beginning
		std::vector<uint8_t> DATA; // data suitable for read/write operations
		const uint8_t* data_ptr = nullptr; // this can either be a memory mapped pointer (read only), or the DATA's pointer
		size_t data_ptr_size = 0;
		bool data_already_decompressed = false;

		std::string fileName; // save to this file on closing if not empty
		std::string directory; // the directory part from the fileName

		std::string name_; // achive name
		Entity entity_ = 0u;

		const uint8_t* thumbnail_data_ptr_write = nullptr; // temp ptr to write archive data
		constexpr const uint8_t* get_thumbnail_data() const { return data_ptr + sizeof(Header); }

		void WriteCompressedData(std::vector<uint8_t>& final_data) const;

		void CreateEmpty(); // creates new archive in write mode

	public:
		// Create empty archive for writing
		Archive();
		Archive(Entity entity, const std::string& name) {
			entity_ = entity; name_ = name; CreateEmpty();
		};
		Archive(const Archive&) = default;
		Archive(Archive&&) = default;
		// Create archive from a file.
		//	If readMode == true, the whole file will be loaded into the archive in read mode
		//	If readMode == false, the file will be written when the archive is destroyed or Close() is called
		Archive(const std::string& fileName, bool readMode = true);
		// Creates a memory mapped archive in read mode
		Archive(const uint8_t* data, size_t size);
		~Archive() { Close(); }

		Archive& operator=(const Archive&) = default;
		Archive& operator=(Archive&&) = default;

		// ----- Custom Settings -----
		const std::string GetArchiveName() const { return name_; }
		const Entity GetArchiveEntity() const { return entity_; }
		void SetArchiveName(const std::string& name) { name_ = name; }
		void SetArchiveEntity(const Entity entity) { entity_ = entity; }
		void ReadData(const uint8_t* data, size_t size);
		void SetFileName(const std::string& fileName);

		void WriteData(std::vector<uint8_t>& dest) const;
		const uint8_t* GetData() const { return data_ptr; }
		const size_t GetSize() const { return data_ptr_size; }
		size_t GetPos() const { return pos; }
		constexpr uint64_t GetVersion() const { return header.version; }
		constexpr bool IsReadMode() const { return readMode; }
		// This can set the archive into either read or write mode, and it will reset it's position
		void SetReadModeAndResetPos(bool isReadMode);
		// Check if the archive has any data
		bool IsOpen() const { return data_ptr != nullptr; };
		// Close the archive.
		//	If it was opened from a file in write mode, the file will be written at this point
		//	The data will be deleted, the archive will be empty after this
		void Close();
		// Write the archive contents to a specific file
		//	The archive data will be written starting from the beginning, to the current position
		bool SaveFile(const std::string& fileName);
		// Write the archive contents into a C++ header file
		//	dataName : it will be the name of the byte data array in the header, that can be memory mapped as an Archive
		bool SaveHeaderFile(const std::string& fileName, const std::string& dataName);
		// Write the archive contents into a C++ source file
 		//	dataName : it will be the name of the byte data array in the header, that can be memory mapped as an Archive
 		//	Note: size is exported as name_size where name is the dataName that you give to it
 		bool SaveCPPFile(const std::string& fileName, const std::string& dataName);
		// If the archive was opened from a file, this will return the file's directory
		const std::string& GetSourceDirectory() const;
		// If the archive was opened from a file, this will return the file's name
		//	The file's name will include the directory as well
		const std::string& GetSourceFileName() const;

		// Set whether the archive should be compressed upon saving
		//	Note that in memory, the archive is uncompressed
		//	Note that compressed archive will not work with streaming!
		constexpr void SetCompressionEnabled(bool value) { header.properties.bits.compressed = value; }
		// Returns true if the archive data is originating from compressed data
		//	Note that even if the archive was opened from compressed data source, the archive is always uncompressed in memory
		//	Note that compressed archive will not work with streaming!
		constexpr bool IsCompressionEnabled() const { return header.properties.bits.compressed; }

		// If Archive contains thumbnail image data, then creates a Texture from it:
		vz::graphics::Texture CreateThumbnailTexture() const;

		// Set a Texture as the thumbnail. This resets the archive, so you should usually do this before writing data:
		void SetThumbnailAndResetPos(const vz::graphics::Texture& texture);

		// Open just the tumbnail data from an archive, and return it as a Texture:
		//	header: optional, can return header info if not null
		static vz::graphics::Texture PeekThumbnail(const std::string& filename, Header* out_header = nullptr);

		// Appends the current archive write offset as uint64_t to the archive
		//	Returns the previous write offset of the archive, which can be used by PatchUnknownJumpPosition()
		//	to write the current archive position to that previous position
		size_t WriteUnknownJumpPosition()
		{
			size_t pos_prev = pos;
			_write(uint64_t(pos));
			return pos_prev;
		}
		// Writes the current archive write offset to the specified archive write offset.
		//	It can be used with Jump() to skip parts of the archive when reading
		void PatchUnknownJumpPosition(size_t offset)
		{
			assert(!readMode);
			assert(!DATA.empty());
			assert(offset + sizeof(uint64_t) < DATA.size());
			*(uint64_t*)(DATA.data() + offset) = uint64_t(pos);
		}
		// Modifies the current archive offset
		//	It can be used in conjunction with WriteUnknownJumpPosition() and PatchUnknownJumpPosition()
		void Jump(uint64_t jump_pos)
		{
			pos = jump_pos;
		}

		// This is like reading a vector<uint8_t>, but instead of copying the data, it returns the memory mapped pointer and size
		inline void MapVector(const uint8_t*& data, size_t& size)
		{
			(*this) >> size;
			data = data_ptr + pos;
			pos += size;
		}

		// It could be templated but we have to be extremely careful of different datasizes on different platforms
		// because serialized data should be interchangeable!
		// So providing exact copy operations for exact types enforces platform agnosticism

		// Write operations
		inline Archive& operator<<(bool data)
		{
			_write((uint32_t)(data ? 1 : 0));
			return *this;
		}
		inline Archive& operator<<(char data)
		{
			_write((int8_t)data);
			return *this;
		}
		inline Archive& operator<<(short data)
		{
			_write((int16_t)data);
			return *this;
		}
		inline Archive& operator<<(unsigned char data)
		{
			_write((uint8_t)data);
			return *this;
		}
		inline Archive& operator<<(unsigned short data)
		{
			_write((uint16_t)data);
			return *this;
		}
		inline Archive& operator<<(int data)
		{
			_write((int64_t)data);
			return *this;
		}
		inline Archive& operator<<(unsigned int data)
		{
			_write((uint64_t)data);
			return *this;
		}
		inline Archive& operator<<(long data)
		{
			_write((int64_t)data);
			return *this;
		}
		inline Archive& operator<<(unsigned long data)
		{
			_write((uint64_t)data);
			return *this;
		}
		inline Archive& operator<<(long long data)
		{
			_write((int64_t)data);
			return *this;
		}
		inline Archive& operator<<(unsigned long long data)
		{
			_write((uint64_t)data);
			return *this;
		}
		inline Archive& operator<<(float data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(double data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const XMFLOAT2& data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const XMFLOAT3& data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const XMFLOAT4& data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const XMFLOAT3X3& data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const XMFLOAT4X3& data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const XMFLOAT4X4& data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const XMUINT2& data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const XMUINT3& data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const XMUINT4& data)
		{
			_write(data);
			return *this;
		}
		inline Archive& operator<<(const vz::Color& data)
		{
			_write(data.rgba);
			return *this;
		}
		inline Archive& operator<<(const std::string& data)
		{
			(*this) << data.length();
			for (const auto& x : data)
			{
				(*this) << x;
			}
			return *this;
		}
		template<typename T>
		inline Archive& operator<<(const std::vector<T>& data)
		{
			// Here we will use the << operator so that non-specified types will have compile error!
			(*this) << data.size();
			for (const T& x : data)
			{
				(*this) << x;
			}
			return *this;
		}
		inline Archive& operator<<(const vz::Archive& other)
		{
			// Here we will use the << operator so that non-specified types will have compile error!
			//	Note: version and thumbnail data is skipped, only data is appended
			const size_t start = sizeof(uint64_t) * 2; // version and thumbnail size
			for (size_t i = start; i < other.pos; ++i)
			{
				(*this) << other.data_ptr[i];
			}
			return *this;
		}

		// Read operations
		inline Archive& operator>>(bool& data)
		{
			uint32_t temp;
			_read(temp);
			data = (temp == 1);
			return *this;
		}
		inline Archive& operator>>(char& data)
		{
			int8_t temp;
			_read(temp);
			data = (char)temp;
			return *this;
		}
		inline Archive& operator>>(short& data)
		{
			int16_t temp;
			_read(temp);
			data = (short)temp;
			return *this;
		}
		inline Archive& operator>>(unsigned char& data)
		{
			uint8_t temp;
			_read(temp);
			data = (unsigned char)temp;
			return *this;
		}
		inline Archive& operator>>(unsigned short& data)
		{
			uint16_t temp;
			_read(temp);
			data = (unsigned short)temp;
			return *this;
		}
		inline Archive& operator>>(int& data)
		{
			int64_t temp;
			_read(temp);
			data = (int)temp;
			return *this;
		}
		inline Archive& operator>>(unsigned int& data)
		{
			uint64_t temp;
			_read(temp);
			data = (unsigned int)temp;
			return *this;
		}
		inline Archive& operator>>(long& data)
		{
			int64_t temp;
			_read(temp);
			data = (long)temp;
			return *this;
		}
		inline Archive& operator>>(unsigned long& data)
		{
			uint64_t temp;
			_read(temp);
			data = (unsigned long)temp;
			return *this;
		}
		inline Archive& operator>>(long long& data)
		{
			int64_t temp;
			_read(temp);
			data = (long long)temp;
			return *this;
		}
		inline Archive& operator>>(unsigned long long& data)
		{
			uint64_t temp;
			_read(temp);
			data = (unsigned long long)temp;
			return *this;
		}
		inline Archive& operator>>(float& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(double& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(XMFLOAT2& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(XMFLOAT3& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(XMFLOAT4& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(XMFLOAT3X3& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(XMFLOAT4X3& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(XMFLOAT4X4& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(XMUINT2& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(XMUINT3& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(XMUINT4& data)
		{
			_read(data);
			return *this;
		}
		inline Archive& operator>>(vz::Color& data)
		{
			_read(data.rgba);
			return *this;
		}
		inline Archive& operator>>(std::string& data)
		{
			uint64_t len;
			(*this) >> len;
			data.resize(len);
			for (size_t i = 0; i < len; ++i)
			{
				(*this) >> data[i];
			}
			return *this;
		}
		template<typename T>
		inline Archive& operator>>(std::vector<T>& data)
		{
			// Here we will use the >> operator so that non-specified types will have compile error!
			size_t count;
			(*this) >> count;
			data.resize(count);
			for (size_t i = 0; i < count; ++i)
			{
				(*this) >> data[i];
			}
			return *this;
		}



	private:

		// This should not be exposed to avoid misaligning data by mistake
		// Any specific type serialization should be implemented by hand
		// But these can be used as helper functions inside this class

		// Write data using memory operations
		template<typename T>
		inline void _write(const T& data)
		{
			assert(!readMode);
			assert(!DATA.empty());
			const size_t _right = pos + sizeof(data);
			if (_right > DATA.size())
			{
				DATA.resize(_right * 2);
				data_ptr = DATA.data();
				data_ptr_size = DATA.size();
			}
			*(T*)(DATA.data() + pos) = data;
			pos = _right;
		}

		// Read data using memory operations
		template<typename T>
		inline void _read(T& data)
		{
			assert(readMode);
			assert(data_ptr != nullptr);
			assert(pos < data_ptr_size);
			data = *(const T*)(data_ptr + pos);
			pos += (size_t)(sizeof(data));
		}
	};
}
