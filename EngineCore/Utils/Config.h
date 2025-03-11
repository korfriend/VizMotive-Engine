#pragma once
//#include "CommonInclude.h"
#pragma warning (disable : 4251)

#ifndef UTIL_EXPORT
#ifdef _WIN32
#define UTIL_EXPORT __declspec(dllexport)
#else
#define UTIL_EXPORT __attribute__((visibility("default")))
#endif
#endif

#include <string>
#include <vector>
#include <unordered_map>

namespace vz::config
{
	struct File;
	struct UTIL_EXPORT Section
	{
		virtual ~Section() = default;

		friend struct File;

		// Check whether the key exists:
		bool Has(const char* name) const;

		// Get the associated value for the key:
		bool GetBool(const char* name) const;
		int GetInt(const char* name) const;
		float GetFloat(const char* name) const;
		std::string GetText(const char* name) const;

		// Set the associated value for the key:
		void Set(const char* name, bool value);
		void Set(const char* name, int value);
		void Set(const char* name, float value);
		void Set(const char* name, const char* value);
		void Set(const char* name, const std::string& value);

		std::unordered_map<std::string, std::string>::iterator begin() { return values.begin(); }
		std::unordered_map<std::string, std::string>::const_iterator begin() const { return values.begin(); }
		std::unordered_map<std::string, std::string>::iterator end() { return values.end(); }
		std::unordered_map<std::string, std::string>::const_iterator end() const { return values.end(); }

	protected:
		std::unordered_map<std::string, std::string> values;
	};

	struct UTIL_EXPORT File : public Section
	{
		// Open a config file (.ini format)
		bool Open(const char* filename);
		// Write back the config file with the current keys and values
		void Commit();
		// Get access to a named section. If it doesn't exist, then it will be created
		Section& GetSection(const char* name);

		std::unordered_map<std::string, Section>::iterator begin() { return sections.begin(); }
		std::unordered_map<std::string, Section>::const_iterator begin() const { return sections.begin(); }
		std::unordered_map<std::string, Section>::iterator end() { return sections.end(); }
		std::unordered_map<std::string, Section>::const_iterator end() const { return sections.end(); }

	private:
		std::string filename;
		std::unordered_map<std::string, Section> sections;
		struct Line
		{
			std::string section_label;
			std::string section_id;
			std::string key;
			std::string comment;
		};
		std::vector<Line> opened_order;
	};

	// Engine Config Helper
	UTIL_EXPORT std::string GetConfigFile(const std::string& sectionName, const std::string& optionName);
	UTIL_EXPORT bool GetBoolConfig(const std::string& sectionName, const std::string& optionName);
	UTIL_EXPORT int GetIntConfig(const std::string& sectionName, const std::string& optionName);
	UTIL_EXPORT float GetFloatConfig(const std::string& sectionName, const std::string& optionName);
	UTIL_EXPORT std::string GetStringConfig(const std::string& sectionName, const std::string& optionName);
}
