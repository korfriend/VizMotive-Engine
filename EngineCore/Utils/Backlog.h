#pragma once
#include <string>

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

	void clear();

	void post(const std::string& input, LogLevel level = LogLevel::Trace);

	void setLogLevel(LogLevel newLevel);

	LogLevel getUnseenLogLevelMax();
};
