#pragma once
#include <memory>
#include <functional>

#ifndef UTIL_EXPORT
#ifdef _WIN32
#define UTIL_EXPORT __declspec(dllexport)
#else
#define UTIL_EXPORT __attribute__((visibility("default")))
#endif
#endif

namespace vz::eventhandler
{
	inline constexpr int EVENT_THREAD_SAFE_POINT = -1;
	inline constexpr int EVENT_RELOAD_SHADERS = -2;
	inline constexpr int EVENT_SET_VSYNC = -3;

	struct Handle
	{
		std::shared_ptr<void> internal_state;
		inline bool IsValid() const { return internal_state.get() != nullptr; }
	};

	UTIL_EXPORT Handle Subscribe(int id, std::function<void(uint64_t)> callback);
	UTIL_EXPORT void Subscribe_Once(int id, std::function<void(uint64_t)> callback);
	UTIL_EXPORT void FireEvent(int id, uint64_t userdata);


	// helper event wrappers can be placed below:
	inline UTIL_EXPORT void SetVSync(bool enabled)
	{
		FireEvent(EVENT_SET_VSYNC, enabled ? 1ull : 0ull);
	}

	void Destroy();
}
