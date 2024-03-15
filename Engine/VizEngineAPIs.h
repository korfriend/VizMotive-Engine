#pragma once
#define __dojostatic extern "C" __declspec(dllexport)
#define __dojoclass class __declspec(dllexport)
#define __dojostruct struct __declspec(dllexport)

#define __FP (float*)&
#define VZRESULT int
#define VZ_OK 0
#define VZ_FAIL 1

// std
#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <set>
#include <any>
#include <list>
#include <memory>

namespace vzm 
{
	template <typename NAME> struct ParamMap {
	private:
		std::string __PM_VERSION = "LIBI_1.4";
		std::unordered_map<NAME, std::any> __params;
	public:
		bool FindParam(const NAME& param_name) {
			auto it = __params.find(param_name);
			return !(it == __params.end());
		}
		template <typename SRCV> bool GetParamCheck(const NAME& param_name, SRCV& param) {
			auto it = __params.find(param_name);
			if (it == __params.end()) return false;
			param = std::any_cast<SRCV&>(it->second);
			return true;
		}
		template <typename SRCV> SRCV GetParam(const NAME& param_name, const SRCV& init_v) {
			auto it = __params.find(param_name);
			if (it == __params.end()) return init_v;
			return std::any_cast<SRCV&>(it->second);
		}
		template <typename SRCV> SRCV* GetParamPtr(const NAME& param_name) {
			auto it = __params.find(param_name);
			if (it == __params.end()) return NULL;
			return (SRCV*)&std::any_cast<SRCV&>(it->second);
		}
		template <typename SRCV, typename DSTV> bool GetParamCastingCheck(const NAME& param_name, DSTV& param) {
			auto it = __params.find(param_name);
			if (it == __params.end()) return false;
			param = (DSTV)std::any_cast<SRCV&>(it->second);
			return true;
		}
		template <typename SRCV, typename DSTV> DSTV GetParamCasting(const NAME& param_name, const DSTV& init_v) {
			auto it = __params.find(param_name);
			if (it == __params.end()) return init_v;
			return (DSTV)std::any_cast<SRCV&>(it->second);
		}
		void SetParam(const NAME& param_name, const std::any& param) {
			__params[param_name] = param;
		}
		void RemoveParam(const NAME& param_name) {
			auto it = __params.find(param_name);
			if (it != __params.end()) {
				__params.erase(it);
			}
		}
		void RemoveAll() {
			__params.clear();
		}
		size_t Size() {
			return __params.size();
		}
		std::string GetPMapVersion() {
			return __PM_VERSION;
		}

		typedef std::unordered_map<NAME, std::any> MapType;
		typename typedef MapType::iterator iterator;
		typename typedef MapType::const_iterator const_iterator;
		typename typedef MapType::reference reference;
		iterator begin() { return __params.begin(); }
		const_iterator begin() const { return __params.begin(); }
		iterator end() { return __params.end(); }
		const_iterator end() const { return __params.end(); }
	};

	__dojostatic VZRESULT InitEngineLib(const std::string& coreName = "VzmEngine", const std::string& logFileName = "EngineApi.log");
	__dojostatic VZRESULT DeinitEngineLib();
}
