#pragma once
#include <memory>

#define MY_API extern "C" __declspec(dllexport)

namespace myParcle {
	MY_API bool     InitEngine(std::shared_ptr<void> spdlogPtr = nullptr);

	MY_API bool SetRenderTargetSize(int w, int h);

	MY_API bool     DoTest();
	MY_API bool     GetRenderTarget();

	MY_API void     DeinitEngine();
}
