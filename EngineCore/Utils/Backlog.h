#pragma once
#include <string>

#ifndef UTIL_EXPORT
#ifdef _WIN32
#define UTIL_EXPORT __declspec(dllexport)
#else
#define UTIL_EXPORT __attribute__((visibility("default")))
#endif
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

	extern "C" UTIL_EXPORT void clear();

	extern "C" UTIL_EXPORT void post(const std::string& input, LogLevel level = LogLevel::Info);

	extern "C" UTIL_EXPORT void setLogLevel(LogLevel newLevel);

	extern "C" UTIL_EXPORT LogLevel getLogLevel();

	void Destroy();
};
