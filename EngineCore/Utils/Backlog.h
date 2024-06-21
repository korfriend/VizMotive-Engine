#pragma once
#include <string>

namespace vz::backlog
{
	enum class LogLevel
	{
		Trace, // SPDLOG_LEVEL_TRACE
		Debug, // SPDLOG_LEVEL_DEBUG
		Info, // SPDLOG_LEVEL_INFO
		Warn, // SPDLOG_LEVEL_WARN
		Error, // SPDLOG_LEVEL_ERROR
		Critical, // SPDLOG_LEVEL_CRITICAL
		None, // SPDLOG_LEVEL_OFF
	};

	void Clear();

	void Post(const std::string& input, LogLevel level = LogLevel::Trace);

	void SetLogLevel(LogLevel newLevel);

	LogLevel GetUnseenLogLevelMax();
};
