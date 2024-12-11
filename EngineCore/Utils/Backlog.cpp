#include "Backlog.h"
#include "Helpers.h"
#include "Platform.h"

#include "../include/spdlog/spdlog.h"
#include "../include/spdlog/sinks/basic_file_sink.h"
#include "../include/spdlog/sinks/stdout_color_sinks.h"

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

	void intialize()
	{
		std::string log_path = helper::GetTempDirectoryPath();

#ifdef PLATFORM_WINDOWS_DESKTOP
		char path[2048];
		HRESULT result = SHGetFolderPathA(NULL, CSIDL_PERSONAL, NULL, 0, path);
		if (result == S_OK)
		{
			log_path = std::string(path) + "/";
		}
#endif

		std::string log_file_path = log_path + "vzEngine.log";

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