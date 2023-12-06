#pragma once
#include <memory>

#define MY_API extern "C" __declspec(dllexport)

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace myParcle {
	MY_API bool     InitEngine(std::shared_ptr<void> spdlogPtr = nullptr);

	MY_API bool		SetShaderCompiler();
	MY_API bool		SetRenderTargetSize(int w, int h);

	MY_API bool     DoTest();
	MY_API bool     GetDX11SharedRenderTarget(ID3D11Device* dx11ImGuiDevice, ID3D11ShaderResourceView** textureSRV, int& w, int& h);

	MY_API void     DeinitEngine();
}
