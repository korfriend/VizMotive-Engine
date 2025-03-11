#include "Backlog.h"
#include "Helpers.h"
#include "Platform.h"

#include "ThirdParty/spdlog/spdlog.h"
#include "ThirdParty/spdlog/sinks/basic_file_sink.h"
#include "ThirdParty/spdlog/sinks/stdout_color_sinks.h"

#include <filesystem>

#ifdef PLATFORM_WINDOWS_DESKTOP
#include <shlobj.h>
#include <codecvt>
#ifdef _DEBUG
#pragma comment(lib,"spdlogd.lib")
#else
#pragma comment(lib,"spdlog.lib")
#endif
#endif // PLATFORM_WINDOWS_DESKTOP

#ifdef PLATFORM_LINUX
#ifdef _DEBUG
#pragma comment(lib,"spdlogd.a")
#else
#pragma comment(lib,"spdlog.a")
#endif
#endif // PLATFORM_LINUX

namespace vz::backlog
{
	std::shared_ptr<spdlog::logger> apiLogger = spdlog::default_logger();

	LogLevel logLevel = LogLevel::Trace;
	bool isInitialized = false;
	std::string logPath;

	const char* GetLogPath() 
	{
		return logPath.c_str();
	}

	static void intialize()
	{
		logPath = helper::GetTempDirectoryPath();

#ifdef PLATFORM_WINDOWS_DESKTOP
		WCHAR path[2048];
		HRESULT result = SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, path);
		if (result == S_OK)
		{
			std::wstring path_w = std::wstring(path);
			helper::StringConvert(path_w, logPath);
#ifdef PLATFORM_WINDOWS_DESKTOP
			// accessible folder test
			std::string initialize_log = "";
			{
				std::string test_file = logPath + "\\vzm_access_test";
				DWORD bytesWritten;
				HANDLE hFile;
				BOOL writeResult = WriteFile(
					hFile,
					test_file.c_str(),
					strlen(test_file.c_str()),
					&bytesWritten,
					NULL
				);
				CloseHandle(hFile);
				DeleteFileA(test_file.c_str());

				DWORD attributes = GetFileAttributesA(logPath.c_str());
				if (!writeResult || attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
				{
					initialize_log = "Invalid CSIDL_PERSONAL: " + logPath + ", switching to CSIDL_LOCAL_APPDATA instead";

					result = SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path);
					path_w = std::wstring(path);
					helper::StringConvert(path_w, logPath);
				}
			}

			logPath += "\\";
#else
			logPath += "/";
#endif
		}
#endif

		std::string log_file_path = logPath + "vzEngine.log";

		apiLogger->set_level(spdlog::level::trace);

		auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path, true);
		auto console_sink = std::make_shared <spdlog::sinks::stdout_color_sink_mt>();
		//apiLogger->sinks().push_back(consoleSink);
		apiLogger->sinks().push_back(file_sink);
		apiLogger->flush_on(spdlog::level::trace);

		apiLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
		apiLogger->info("Log Initialized with Path : " + log_file_path);

		isInitialized = true;
	}
	
	void Destroy()
	{
		apiLogger.reset();
	}

	void clear()
	{
		intialize();
	}

	std::mutex mutex;
	void postThreadSafe(const std::string& input, LogLevel level)
	{
		std::lock_guard<std::mutex> lock(mutex);
		post(input, level);
	}

	void post(const std::string& input, LogLevel level)
	{
		if (!isInitialized)
		{
			intialize();
		}
		switch (level)
		{
		case LogLevel::Trace:
			apiLogger->trace(input); break;
		case LogLevel::Debug:
			apiLogger->debug(input); break;
		case LogLevel::Info:
			apiLogger->info(input); break;
		case LogLevel::Warn:
			apiLogger->warn(input); break;
		case LogLevel::Error:
			apiLogger->error(input); break;
		case LogLevel::Critical:
			apiLogger->critical(input); break;
		case LogLevel::None:
		default:
				return;
		}
	}

	void setLogLevel(LogLevel newLevel)
	{
		logLevel = newLevel;
		apiLogger->set_level((spdlog::level::level_enum)newLevel);		
	}

	LogLevel getLogLevel()
	{
		return logLevel;
	}
}