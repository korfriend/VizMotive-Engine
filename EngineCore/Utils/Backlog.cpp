#include "Backlog.h"
#include "Helpers.h"

#include "../includes/spdlog/spdlog.h"
#include "../includes/spdlog/sinks/basic_file_sink.h"
#include "../includes/spdlog/sinks/stdout_color_sinks.h"

#include <filesystem>

#ifdef PLATFORM_WINDOWS_DESKTOP
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
		std::string log_file_path = log_path + "/vzEngine.txt";

		apiLogger->set_level(spdlog::level::trace);

		auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file_path, true);
		auto consoleSink = std::make_shared <spdlog::sinks::stdout_color_sink_mt>();
		apiLogger->sinks().push_back(consoleSink);
		apiLogger->sinks().push_back(fileSink);
		apiLogger->flush_on(spdlog::level::trace);

		apiLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
		apiLogger->info("Log Initialized with Path : " + log_file_path);

		isInitialized = true;
	}

	void Clear()
	{
		intialize();
	}

	void Post(const std::string& input, LogLevel level)
	{
		if (!isInitialized)
		{
			intialize();
		}
		switch (logLevel)
		{
		case LogLevel::Trace:
			apiLogger->trace(input);
		case LogLevel::Debug:
			apiLogger->debug(input);
		case LogLevel::Info:
			apiLogger->info(input);
		case LogLevel::Warn:
			apiLogger->warn(input);
		case LogLevel::Error:
			apiLogger->error(input);
		case LogLevel::Critical:
			apiLogger->critical(input);
		case LogLevel::None:
		default:
				return;
		}
	}

	void SetLogLevel(LogLevel newLevel)
	{
		logLevel = newLevel;
		apiLogger->set_level((spdlog::level::level_enum)newLevel);		
	}

	LogLevel GetUnseenLogLevelMax()
	{
		return logLevel;
	}
}