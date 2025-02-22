#pragma once
// This file includes platform, os specific libraries and supplies common platform specific resources

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif // NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <SDKDDKVer.h>
#include <windows.h>
#include <tchar.h>
#include <unordered_map>
#include <string>

#if WINAPI_FAMILY
#define PLATFORM_WINDOWS_DESKTOP
#endif // WINAPI_FAMILY_GAMES
#define vzLoadLibrary(name) LoadLibraryA(name)
#define vzGetProcAddress(handle,name) GetProcAddress(handle, name)
#else
#define PLATFORM_LINUX
#include <dlfcn.h>
#define vzLoadLibrary(name) dlopen(name, RTLD_LAZY)
#define vzGetProcAddress(handle,name) dlsym(handle, name)
typedef void* HMODULE;
#endif // _WIN32

#ifdef SDL2
#include <SDL2/SDL.h>
#include <SDL_vulkan.h>
#include "sdl2.h"
#endif


namespace vz::platform
{
#ifdef _WIN32
	using window_type = HWND;
	using error_type = HRESULT;
#elif SDL2
	using window_type = SDL_Window*;
	using error_type = int;
#else
	using window_type = void*;
	using error_type = int;
#endif // _WIN32

	inline void Exit()
	{
#ifdef _WIN32
		PostQuitMessage(0);
#endif // _WIN32
#ifdef SDL2
		SDL_Event quit_event;
		quit_event.type = SDL_QUIT;
		SDL_PushEvent(&quit_event);
#endif
	}

	struct WindowProperties
	{
		int width = 0;
		int height = 0;
		float dpi = 96;
	};
	inline void GetWindowProperties(window_type window, WindowProperties* dest)
	{
#ifdef PLATFORM_WINDOWS_DESKTOP
		dest->dpi = (float)GetDpiForWindow(window);
#endif // WINDOWS_DESKTOP

#ifdef PLATFORM_XBOX
		dest->dpi = 96.f;
#endif // PLATFORM_XBOX

#if defined(PLATFORM_WINDOWS_DESKTOP) || defined(PLATFORM_XBOX)
		RECT rect;
		GetClientRect(window, &rect);
		dest->width = int(rect.right - rect.left);
		dest->height = int(rect.bottom - rect.top);
#endif // PLATFORM_WINDOWS_DESKTOP || PLATFORM_XBOX

#ifdef PLATFORM_LINUX
		int window_width, window_height;
		SDL_GetWindowSize(window, &window_width, &window_height);
		SDL_Vulkan_GetDrawableSize(window, &dest->width, &dest->height);
		dest->dpi = ((float)dest->width / (float)window_width) * 96.f;
#endif // PLATFORM_LINUX
	}

	template <typename T>
	T LoadModule(std::string moduleName, std::string functionName, std::unordered_map<std::string, HMODULE>& importedModules)
	{
		HMODULE hMouleLib = NULL;
		auto it = importedModules.find(moduleName);
		if (it == importedModules.end())
		{
			hMouleLib = vzLoadLibrary(moduleName.c_str());
			if (hMouleLib != NULL)
				importedModules[moduleName] = hMouleLib;
		}
		else
			hMouleLib = it->second;
		if (hMouleLib == NULL)
		{
			return NULL;
		}

		T lpdll_function = NULL;
		lpdll_function = (T)vzGetProcAddress(hMouleLib, functionName.c_str());
		return lpdll_function;
	}
}
