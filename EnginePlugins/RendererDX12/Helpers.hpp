#pragma once
#include "Libs/Math.h"

#include <iostream>
#include <fstream>
#include <filesystem>

namespace helpers // simple version of Helpers.cpp
{
	inline void StringConvert(const std::string& from, std::wstring& to)
	{
#ifdef _WIN32
		int num = MultiByteToWideChar(CP_UTF8, 0, from.c_str(), -1, NULL, 0);
		if (num > 0)
		{
			to.resize(size_t(num) - 1);
			MultiByteToWideChar(CP_UTF8, 0, from.c_str(), -1, &to[0], num);
		}
#else
		std::wstring_convert<std::codecvt_utf8<wchar_t>> cv;
		to = cv.from_bytes(from);
#endif // _WIN32
	}
	inline void StringConvert(const std::wstring& from, std::string& to)
	{
#ifdef _WIN32
		int num = WideCharToMultiByte(CP_UTF8, 0, from.c_str(), -1, NULL, 0, NULL, NULL);
		if (num > 0)
		{
			to.resize(size_t(num) - 1);
			WideCharToMultiByte(CP_UTF8, 0, from.c_str(), -1, &to[0], num, NULL, NULL);
		}
#else
		std::wstring_convert<std::codecvt_utf8<wchar_t>> cv;
		to = cv.to_bytes(from);
#endif // _WIN32
	}
	inline int StringConvert(const char* from, wchar_t* to, int dest_size_in_characters = -1)
	{
#ifdef _WIN32
		int num = MultiByteToWideChar(CP_UTF8, 0, from, -1, NULL, 0);
		if (num > 0)
		{
			if (dest_size_in_characters >= 0)
			{
				num = std::min(num, dest_size_in_characters);
			}
			MultiByteToWideChar(CP_UTF8, 0, from, -1, &to[0], num);
		}
		return num;
#else
		std::wstring_convert<std::codecvt_utf8<wchar_t>> cv;
		auto result = cv.from_bytes(from).c_str();
		int num = (int)cv.converted();
		if (dest_size_in_characters >= 0)
		{
			num = std::min(num, dest_size_in_characters);
		}
		std::memcpy(to, result, num * sizeof(wchar_t));
		return num;
#endif // _WIN32
	}
	inline int StringConvert(const wchar_t* from, char* to, int dest_size_in_characters = -1)
	{
#ifdef _WIN32
		int num = WideCharToMultiByte(CP_UTF8, 0, from, -1, NULL, 0, NULL, NULL);
		if (num > 0)
		{
			if (dest_size_in_characters >= 0)
			{
				num = std::min(num, dest_size_in_characters);
			}
			WideCharToMultiByte(CP_UTF8, 0, from, -1, &to[0], num, NULL, NULL);
		}
		return num;
#else
		std::wstring_convert<std::codecvt_utf8<wchar_t>> cv;
		auto result = cv.to_bytes(from).c_str();
		int num = (size_t)cv.converted();
		if (dest_size_in_characters >= 0)
		{
			num = std::min(num, dest_size_in_characters);
		}
		std::memcpy(to, result, num * sizeof(char));
		return num;
#endif // _WIN32
	}

	template <class T>
	constexpr void hash_combine(std::size_t& seed, const T& v)
	{
		std::hash<T> hasher;
		seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
	}

	inline void messageBox(const std::string& msg, const std::string& caption)
	{
#ifdef PLATFORM_WINDOWS_DESKTOP
		MessageBoxA(GetActiveWindow(), msg.c_str(), caption.c_str(), 0);
#endif // PLATFORM_WINDOWS_DESKTOP

#ifdef SDL2
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, caption.c_str(), msg.c_str(), NULL);
#endif // SDL2
	}

#ifdef _WIN32
	// On windows we need to expand UTF8 strings to UTF16 when passing it to WinAPI:
	inline std::wstring ToNativeString(const std::string& fileName)
	{
		std::wstring fileName_wide;
		StringConvert(fileName, fileName_wide);
		return fileName_wide;
	}
#else
#define ToNativeString(x) (x)
#endif // _WIN32

	template<template<typename T, typename A> typename vector_interface>
	inline bool FileRead_Impl(const std::string& fileName, vector_interface<uint8_t, std::allocator<uint8_t>>& data, size_t max_read, size_t offset)
	{
#if defined(PLATFORM_LINUX)
		std::string filepath = fileName;
		std::replace(filepath.begin(), filepath.end(), '\\', '/'); // Linux cannot handle backslash in file path, need to convert it to forward slash
		std::ifstream file(filepath, std::ios::binary | std::ios::ate);
#else
		std::ifstream file(ToNativeString(fileName), std::ios::binary | std::ios::ate);
#endif // PLATFORM_LINUX 

		if (file.is_open())
		{
			size_t dataSize = (size_t)file.tellg() - offset;
			dataSize = std::min(dataSize, max_read);
			file.seekg((std::streampos)offset);
			data.resize(dataSize);
			file.read((char*)data.data(), dataSize);
			file.close();
			return true;
		}

		messageBox("File not found: " + fileName, "ERROR0");
		return false;
	}

	inline bool FileRead(const std::string& fileName, std::vector<uint8_t>& data, size_t max_read = ~0ull, size_t offset = 0)
	{
		return FileRead_Impl(fileName, data, max_read, offset);
	}

	inline bool FileWrite(const std::string& fileName, const uint8_t* data, size_t size)
	{
		if (size <= 0)
		{
			return false;
		}

		std::ofstream file(ToNativeString(fileName), std::ios::binary | std::ios::trunc);
		if (file.is_open())
		{
			file.write((const char*)data, (std::streamsize)size);
			file.close();
			return true;
		}

		return false;
	}

	inline bool FileExists(const std::string& fileName)
	{
		bool exists = std::filesystem::exists(ToNativeString(fileName));
		return exists;
	}

	inline void SplitPath(const std::string& fullPath, std::string& dir, std::string& fileName)
	{
		size_t found;
		found = fullPath.find_last_of("/\\");
		dir = fullPath.substr(0, found + 1);
		fileName = fullPath.substr(found + 1);
	}

	inline std::string GetCurrentPath()
	{
		auto path = std::filesystem::current_path();
		return path.string();// generic_u8string();
	}

	inline std::string GetExtensionFromFileName(const std::string& filename)
	{
		size_t idx = filename.rfind('.');

		if (idx != std::string::npos)
		{
			std::string extension = filename.substr(idx + 1);
			return extension;
		}

		// No extension found
		return "";
	}

	inline std::string GetFileNameFromPath(const std::string& fullPath)
	{
		if (fullPath.empty())
		{
			return fullPath;
		}

		std::string ret, empty;
		SplitPath(fullPath, empty, ret);
		return ret;
	}

	inline std::string GetDirectoryFromPath(const std::string& fullPath)
	{
		if (fullPath.empty())
		{
			return fullPath;
		}

		std::string ret, empty;
		SplitPath(fullPath, ret, empty);
		return ret;
	}

	inline void DirectoryCreate(const std::string& path)
	{
		std::filesystem::create_directories(ToNativeString(path));
	}

	inline std::string ReplaceExtension(const std::string& filename, const std::string& extension)
	{
		size_t idx = filename.rfind('.');

		if (idx == std::string::npos)
		{
			// extension not found, append it:
			return filename + "." + extension;
		}
		return filename.substr(0, idx + 1) + extension;
	}

	inline std::string RemoveExtension(const std::string& filename)
	{
		size_t idx = filename.rfind('.');

		if (idx == std::string::npos)
		{
			// extension not found:
			return filename;
		}
		return filename.substr(0, idx);
	}

	inline void MakePathAbsolute(std::string& path)
	{
		std::filesystem::path absolute = std::filesystem::absolute(ToNativeString(path));
		if (!absolute.empty())
		{
			path = absolute.string();// generic_u8string();
		}
	}

	inline void MakePathRelative(const std::string& rootdir, std::string& path)
	{
		if (rootdir.empty() || path.empty())
		{
			return;
		}

		std::filesystem::path filepath = ToNativeString(path);
		if (filepath.is_absolute())
		{
			std::filesystem::path rootpath = ToNativeString(rootdir);
			std::filesystem::path relative = std::filesystem::relative(filepath, rootpath);
			if (!relative.empty())
			{
				path = relative.string();//.generic_u8string();
			}
		}
	}

	inline uint64_t FileTimestamp(const std::string& fileName)
	{
		if (!FileExists(fileName))
			return 0;
		auto tim = std::filesystem::last_write_time(ToNativeString(fileName));
		return std::chrono::duration_cast<std::chrono::duration<uint64_t>>(tim.time_since_epoch()).count();
	}

	class FileWrapper	// simple version of Archive
	{
	private:
		bool readMode = false; // archive can be either read or write mode, but not both
		size_t pos = 0; // position of the next memory operation, relative to the data's beginning
		const uint8_t* data_ptr = nullptr; // this can either be a memory mapped pointer (read only), or the DATA's pointer
		size_t data_ptr_size = 0;
		std::vector<uint8_t> DATA; // data suitable for read/write operations

		std::string fileName; // save to this file on closing if not empty
		std::string directory; // the directory part from the fileName

		size_t thumbnail_data_size = 0;
		const uint8_t* thumbnail_data_ptr = nullptr;

		void CreateEmpty()
		{
			DATA.resize(128); // starting size
			data_ptr = DATA.data();
			data_ptr_size = DATA.size();
			SetReadModeAndResetPos(false);
		}
	public:
		//	If readMode == true, the whole file will be loaded into the archive in read mode
		//	If readMode == false, the file will be written when the archive is destroyed or Close() is called
		FileWrapper(const std::string& fileName, bool readMode = true)
			: readMode(readMode), fileName(fileName)
		{
			if (!fileName.empty())
			{
				directory = GetDirectoryFromPath(fileName);
				if (readMode)
				{
					if (FileRead(fileName, DATA))
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

		// Creates a memory mapped archive in read mode
		~FileWrapper() { Close(); }

		bool SaveFile(const std::string& fileName)
		{
			return FileWrite(fileName, data_ptr, pos);
		}
		const uint8_t* GetData() const { return data_ptr; }
		const size_t GetSize() const { return data_ptr_size; }
		size_t GetPos() const { return pos; }
		constexpr bool IsReadMode() const { return readMode; }
		// This can set the archive into either read or write mode, and it will reset it's position
		void SetReadModeAndResetPos(bool isReadMode)
		{
			readMode = isReadMode;
			pos = 0;

			if (readMode)
			{
				(*this) >> thumbnail_data_size;
				thumbnail_data_ptr = data_ptr + pos;
				pos += thumbnail_data_size;
			}
			else
			{
				(*this) << thumbnail_data_size;
				const uint8_t* thumbnail_data_dst = data_ptr + pos;
				for (size_t i = 0; i < thumbnail_data_size; ++i)
				{
					(*this) << thumbnail_data_ptr[i];
				}
				thumbnail_data_ptr = thumbnail_data_dst;
			}
		}
		// Check if the archive has any data
		bool IsOpen() const { return data_ptr != nullptr; };
		// Close the archive.
		//	If it was opened from a file in write mode, the file will be written at this point
		//	The data will be deleted, the archive will be empty after this
		void Close()
		{
			if (!readMode && !fileName.empty())
			{
				SaveFile(fileName);
			}
			DATA.clear();
		}
		// If the archive was opened from a file, this will return the file's directory
		const std::string& GetSourceDirectory() const
		{
			return directory;
		}
		// If the archive was opened from a file, this will return the file's name
		//	The file's name will include the directory as well
		const std::string& GetSourceFileName() const
		{
			return fileName;
		}

		// Write operations
		inline FileWrapper& operator<<(bool data)
		{
			_write((uint32_t)(data ? 1 : 0));
			return *this;
		}
		inline FileWrapper& operator<<(char data)
		{
			_write((int8_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(short data)
		{
			_write((int16_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(unsigned char data)
		{
			_write((uint8_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(unsigned short data)
		{
			_write((uint16_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(int data)
		{
			_write((int64_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(unsigned int data)
		{
			_write((uint64_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(long data)
		{
			_write((int64_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(unsigned long data)
		{
			_write((uint64_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(long long data)
		{
			_write((int64_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(unsigned long long data)
		{
			_write((uint64_t)data);
			return *this;
		}
		inline FileWrapper& operator<<(float data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(double data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const XMFLOAT2& data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const XMFLOAT3& data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const XMFLOAT4& data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const XMFLOAT3X3& data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const XMFLOAT4X3& data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const XMFLOAT4X4& data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const XMUINT2& data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const XMUINT3& data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const XMUINT4& data)
		{
			_write(data);
			return *this;
		}
		inline FileWrapper& operator<<(const std::string& data)
		{
			(*this) << data.length();
			for (const auto& x : data)
			{
				(*this) << x;
			}
			return *this;
		}
		template<typename T>
		inline FileWrapper& operator<<(const std::vector<T>& data)
		{
			// Here we will use the << operator so that non-specified types will have compile error!
			(*this) << data.size();
			for (const T& x : data)
			{
				(*this) << x;
			}
			return *this;
		}

		// Read operations
		inline FileWrapper& operator>>(bool& data)
		{
			uint32_t temp;
			_read(temp);
			data = (temp == 1);
			return *this;
		}
		inline FileWrapper& operator>>(char& data)
		{
			int8_t temp;
			_read(temp);
			data = (char)temp;
			return *this;
		}
		inline FileWrapper& operator>>(short& data)
		{
			int16_t temp;
			_read(temp);
			data = (short)temp;
			return *this;
		}
		inline FileWrapper& operator>>(unsigned char& data)
		{
			uint8_t temp;
			_read(temp);
			data = (unsigned char)temp;
			return *this;
		}
		inline FileWrapper& operator>>(unsigned short& data)
		{
			uint16_t temp;
			_read(temp);
			data = (unsigned short)temp;
			return *this;
		}
		inline FileWrapper& operator>>(int& data)
		{
			int64_t temp;
			_read(temp);
			data = (int)temp;
			return *this;
		}
		inline FileWrapper& operator>>(unsigned int& data)
		{
			uint64_t temp;
			_read(temp);
			data = (unsigned int)temp;
			return *this;
		}
		inline FileWrapper& operator>>(long& data)
		{
			int64_t temp;
			_read(temp);
			data = (long)temp;
			return *this;
		}
		inline FileWrapper& operator>>(unsigned long& data)
		{
			uint64_t temp;
			_read(temp);
			data = (unsigned long)temp;
			return *this;
		}
		inline FileWrapper& operator>>(long long& data)
		{
			int64_t temp;
			_read(temp);
			data = (long long)temp;
			return *this;
		}
		inline FileWrapper& operator>>(unsigned long long& data)
		{
			uint64_t temp;
			_read(temp);
			data = (unsigned long long)temp;
			return *this;
		}
		inline FileWrapper& operator>>(float& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(double& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(XMFLOAT2& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(XMFLOAT3& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(XMFLOAT4& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(XMFLOAT3X3& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(XMFLOAT4X3& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(XMFLOAT4X4& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(XMUINT2& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(XMUINT3& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(XMUINT4& data)
		{
			_read(data);
			return *this;
		}
		inline FileWrapper& operator>>(std::string& data)
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
		inline FileWrapper& operator>>(std::vector<T>& data)
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