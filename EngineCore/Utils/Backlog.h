#pragma once
#include <string>
#include <inttypes.h>

#ifndef UTIL_EXPORT
#ifdef _WIN32
#define UTIL_EXPORT __declspec(dllexport)
#else
#define UTIL_EXPORT __attribute__((visibility("default")))
#endif
#endif

#define vzlog_level(str,level,...) {char text[1024]; snprintf(text, sizeof(text), str, ## __VA_ARGS__); vz::backlog::post(text, level);}
#define vzlog_messagebox(str,...) {char text[1024]; snprintf(text, sizeof(text), str, ## __VA_ARGS__); vz::backlog::post(text, vz::backlog::LogLevel::Error); vz::helper::messageBox(text, "Error!");}
#define vzlog_warning(str,...) {vzlog_level(str, vz::backlog::LogLevel::Warn, ## __VA_ARGS__);}
#define vzlog_error(str,...) {vzlog_level(str, vz::backlog::LogLevel::Error, ## __VA_ARGS__);}
#define vzlog(str,...) {vzlog_level(str, vz::backlog::LogLevel::Info, ## __VA_ARGS__);}
#define vzlog_assert(cond,str,...) {if(!(cond)){vzlog_error(str, ## __VA_ARGS__); assert(cond);}}

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

	extern "C" UTIL_EXPORT void postThreadSafe(const std::string& input, LogLevel level = LogLevel::Info);

	extern "C" UTIL_EXPORT void setLogLevel(LogLevel newLevel);

	extern "C" UTIL_EXPORT LogLevel getLogLevel();

	extern "C" UTIL_EXPORT const char* GetLogPath();
};

namespace vz
{
	using LogLevel = backlog::LogLevel;
}
