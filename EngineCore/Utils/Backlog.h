#pragma once
#include <string>

#ifdef _WIN32
#define UTIL_EXPORT __declspec(dllexport)
#else
#define UTIL_EXPORT __attribute__((visibility("default")))
#endif

namespace vz::backlog
{
	enum class LogLevel
	{
		Trace = 0, // SPDLOG_LEVEL_TRACE
		Debug, // SPDLOG_LEVEL_DEBUG
		Info, // SPDLOG_LEVEL_INFO
		Warn, // SPDLOG_LEVEL_WARN
		Error, // SPDLOG_LEVEL_ERROR
		Critical, // SPDLOG_LEVEL_CRITICAL
		None, // SPDLOG_LEVEL_OFF
	};

	void UTIL_EXPORT clear();

	void UTIL_EXPORT post(const std::string& input, LogLevel level = LogLevel::Trace);

	void UTIL_EXPORT setLogLevel(LogLevel newLevel);

	LogLevel UTIL_EXPORT getLogLevel();
};
