#include "MyEngineAPI.h"

#include <d3d11.h>
#include <d3dcompiler.h>

#include "SimpleMath.h"
using namespace DirectX::SimpleMath;

#include <wrl.h> 
#include <spdlog/spdlog.h>
#include <iostream>
#include <fstream>

using namespace Microsoft::WRL;
using namespace std;

std::shared_ptr<spdlog::logger> g_apiLogger;

static ComPtr<ID3D11Device> g_device;
static ComPtr<ID3D11DeviceContext> g_context;

static ComPtr<ID3D11RenderTargetView> g_viewRT;
static ComPtr<ID3D11DepthStencilView> g_viewDS;
static ComPtr<ID3D11RasterizerState> g_myRS;
static ComPtr<ID3D11DepthStencilState> g_myDSS;

static ComPtr<ID3D11InputLayout> g_inputLayerP;
static ComPtr<ID3D11Buffer> g_testGeoVertexBuffer;
static ComPtr<ID3D11Buffer> g_myCB;
static ComPtr<ID3D11VertexShader> g_myVS;
static ComPtr<ID3D11PixelShader> g_myPS;
static ComPtr<ID3D11ShaderResourceView> g_sharedSRV;

static int g_rt_w = 0, g_rt_h = 0;

Vector3 g_quadPts[] = { Vector3(-1, 1, 0.5), Vector3(1, 1, 0.5), Vector3(1, -1, 0.5), Vector3(-1, -1, 0.5) };

typedef Vector3 float3;
typedef Vector2 float2;
typedef Matrix float4x4;

struct PostRenderer {
	float3 posCam; // WS
	int lightColor;

	float3 posLight; // WS
	float lightIntensity;

	float4x4 matPS2WS;

	float2 rtSize;
	float2 dummy1;

	float3 disBoxCenter; // WS
	float distBoxSize; // WS
};

PostRenderer g_cbQuadRenderer;

auto FailRet = [](const std::string& err) {
	g_apiLogger->error(err);
	return false;
};

bool myParcle::InitEngine(std::shared_ptr<void> spdlogPtr)
{
	g_apiLogger = std::static_pointer_cast<spdlog::logger, void>(spdlogPtr);

	// D3D_DRIVER_TYPE_HARDWARE 대신 D3D_DRIVER_TYPE_WARP 사용해보세요
	// const D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_WARP;
	const D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;

	// 여기서 생성하는 것들
	// m_device, m_context, m_swapChain,
	// m_renderTargetView, m_screenViewport, m_rasterizerSate

	// m_device와 m_context 생성

	UINT createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	const D3D_FEATURE_LEVEL featureLevels[2] = {
		D3D_FEATURE_LEVEL_11_0, // 더 높은 버전이 먼저 오도록 설정
		D3D_FEATURE_LEVEL_9_3 };

	D3D_FEATURE_LEVEL featureLevel;

	if (FAILED(D3D11CreateDevice(
		nullptr,                  // Specify nullptr to use the default adapter.
		driverType,               // Create a device using the hardware graphics driver.
		0,                        // Should be 0 unless the driver is D3D_DRIVER_TYPE_SOFTWARE.
		createDeviceFlags,        // Set debug and Direct2D compatibility flags.
		featureLevels,            // List of feature levels this app can support.
		ARRAYSIZE(featureLevels), // Size of the list above.
		D3D11_SDK_VERSION, // Always set this to D3D11_SDK_VERSION for Microsoft Store apps.
		&g_device,           // Returns the Direct3D device created.
		&featureLevel,     // Returns feature level of device created.
		&g_context           // Returns the device immediate context.
	))) {
		return FailRet("D3D11CreateDevice() failed.");
	}

	D3D11_DEPTH_STENCIL_DESC descDepthStencil = {};
	descDepthStencil.DepthEnable = TRUE;
	descDepthStencil.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	descDepthStencil.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	descDepthStencil.StencilEnable = FALSE;
	g_device->CreateDepthStencilState(&descDepthStencil, &g_myDSS);

	D3D11_RASTERIZER_DESC descRaster = {};
	descRaster.FillMode = D3D11_FILL_SOLID;
	descRaster.CullMode = D3D11_CULL_BACK;
	descRaster.FrontCounterClockwise = FALSE;
	descRaster.DepthBias = 0;
	descRaster.DepthBiasClamp = 0;
	descRaster.SlopeScaledDepthBias = 0;
	descRaster.DepthClipEnable = TRUE;
	descRaster.ScissorEnable = FALSE;
	descRaster.MultisampleEnable = FALSE;
	descRaster.AntialiasedLineEnable = FALSE;
	if (FAILED(g_device->CreateRasterizerState(&descRaster, g_myRS.ReleaseAndGetAddressOf()))) {
		return FailRet("CreateRasterizerState Failed.");
	}

	D3D11_BUFFER_DESC dcCBuffer = {};
	dcCBuffer.ByteWidth = sizeof(Renderer);
	dcCBuffer.Usage = D3D11_USAGE_DYNAMIC;
	dcCBuffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	dcCBuffer.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	dcCBuffer.MiscFlags = 0;

	if (FAILED(g_device->CreateBuffer(&dcCBuffer, NULL, g_myCB.ReleaseAndGetAddressOf()))) {
		return FailRet("CreateBuffer Failed.");
	}

	if (!SetShaderCompiler()) {
		return false;
	}

	D3D11_BUFFER_DESC dcVB_Quad = {};
	dcVB_Quad.ByteWidth = sizeof(Vector3) * 4;
	dcVB_Quad.Usage = D3D11_USAGE_IMMUTABLE;
	dcVB_Quad.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	dcVB_Quad.CPUAccessFlags = 0;
	dcVB_Quad.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA sresVB = {};
	sresVB.pSysMem = g_quadPts;
	sresVB.SysMemPitch = 0;
	sresVB.SysMemSlicePitch = 0;

	if (FAILED(g_device->CreateBuffer(&dcVB_Quad, &sresVB, g_testGeoVertexBuffer.ReleaseAndGetAddressOf()))) {
		return FailRet("CreateBuffer Failed.");
	}

	return true;
}

bool GetEnginePath(std::string& enginePath)
{
	using namespace std;
	char ownPth[2048];
	GetModuleFileNameA(NULL, ownPth, (sizeof(ownPth)));
	string exe_path = ownPth;
	string exe_path_;
	size_t pos = 0;
	std::string token;
	string delimiter = "\\";
	while ((pos = exe_path.find(delimiter)) != std::string::npos) {
		token = exe_path.substr(0, pos);
		if (token.find(".exe") != std::string::npos) break;
		exe_path += token + "\\";
		exe_path_ += token + "\\";
		exe_path.erase(0, pos + delimiter.length());
	}

	ifstream file(exe_path + "..\\engine_module_path.txt");
	if (file.is_open()) {
		getline(file, enginePath);
		file.close();
		return true;
	}
	return false;
}

bool myParcle::SetShaderCompiler()
{
	UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
	compileFlags |= D3DCOMPILE_DEBUG;
#endif

	//ComPtr<ID3DBlob> byteCode;
	//ComPtr<ID3DBlob> errors;
	// Compile vertex shader
	//HRESULT hr = D3DCompileFromFile(L"../MyEngine/hlsl/Color_VS.hlsl", nullptr, nullptr, 
	//	"VSMain", "vs_5_0", compileFlags, 0, byteCode.GetAddressOf(), errors.GetAddressOf());
	//if (FAILED(hr) || errors != nullptr) {
	//	return FailRet("D3DCompileFromFile Failed.");
	//}

	std::string enginePath;
	if (!GetEnginePath(enginePath)) {
		return FailRet("Failure to Read Engine Path.");
	}

	auto RegisterShaderObjFile = [&enginePath](const std::string& shaderObjFileName, const std::string& shaderProfile) -> ID3D11DeviceChild*
	{
		FILE* pFile;
		if (fopen_s(&pFile, (enginePath + "/hlsl/obj/" + shaderObjFileName).c_str(), "rb") == 0)
		{
			fseek(pFile, 0, SEEK_END);
			unsigned long long ullFileSize = ftell(pFile);
			fseek(pFile, 0, SEEK_SET);
			unsigned char* pyRead = new unsigned char[ullFileSize];
			fread(pyRead, sizeof(unsigned char), ullFileSize, pFile);
			fclose(pFile);

			if (shaderProfile == "VS") {
				ID3D11VertexShader* pdx11VShader = NULL;
				if (FAILED(g_device->CreateVertexShader(pyRead, ullFileSize, nullptr, &pdx11VShader))) {
					g_apiLogger->error("CreateVertexShader Failed.");
					return NULL;
				}

				D3D11_INPUT_ELEMENT_DESC inlayerDcP[] =
				{
					{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				};
				if (g_device->CreateInputLayout(inlayerDcP, 1, pyRead, ullFileSize, g_inputLayerP.ReleaseAndGetAddressOf()) != S_OK) {
					pdx11VShader->Release();
					g_apiLogger->error("CreateInputLayout Failed.");
					return NULL;
				}

				return pdx11VShader;
			}
			else if (shaderProfile == "PS") {
				ID3D11PixelShader* pdx11PShader = NULL;
				if (FAILED(g_device->CreatePixelShader(pyRead, ullFileSize, nullptr, &pdx11PShader))) {
					g_apiLogger->error("CreatePixelShader Failed.");
					return NULL;
				}
				return pdx11PShader;
			}
		}

		return NULL;
	};

	ID3D11VertexShader* pdx11VShader = (ID3D11VertexShader*)RegisterShaderObjFile("VS_QUAD", "VS");
	if (pdx11VShader != NULL) {
		g_myVS = pdx11VShader;
	}
	ID3D11PixelShader* pdx11PShader = (ID3D11PixelShader*)RegisterShaderObjFile("PS_RayMARCH", "PS");
	if (pdx11VShader != NULL) {
		g_myPS = pdx11PShader;
	}

	return true;
}

bool myParcle::SetRenderTargetSize(int w, int h)
{
	g_rt_w = w, g_rt_h = h;

	ComPtr<ID3D11Texture2D> texRT;
	
	D3D11_TEXTURE2D_DESC descTex2d = {};
	descTex2d.Width = w;
	descTex2d.Height = h;
	descTex2d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	descTex2d.MipLevels = 0;
	descTex2d.ArraySize = 1;
	descTex2d.Usage = D3D11_USAGE_DEFAULT;
	descTex2d.CPUAccessFlags = NULL;
	descTex2d.SampleDesc.Count = 1;
	descTex2d.SampleDesc.Quality = 0;
	descTex2d.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
	descTex2d.BindFlags = D3D11_BIND_RENDER_TARGET;
	if (g_device->CreateTexture2D(&descTex2d, nullptr, &texRT) != S_OK) {
		return FailRet("CreateTexture2D( RT ) failed.");
	}

	D3D11_RENDER_TARGET_VIEW_DESC descRTV = {};
	descRTV.Format = descTex2d.Format;
	descRTV.Texture2D.MipSlice = 0;
	descRTV.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	HRESULT hr = g_device->CreateRenderTargetView(texRT.Get(), &descRTV, g_viewRT.ReleaseAndGetAddressOf());
	texRT.Reset();

	if (hr != S_OK) {
		return FailRet("CreateRenderTargetView() failed.");
	}
	
	ComPtr<ID3D11Texture2D> texDS;
	descTex2d.Format = DXGI_FORMAT_D32_FLOAT;
	descTex2d.MipLevels = 0;
	descTex2d.ArraySize = 1;
	descTex2d.Usage = D3D11_USAGE_DEFAULT;
	descTex2d.MiscFlags = NULL;
	descTex2d.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (g_device->CreateTexture2D(&descTex2d, nullptr, &texDS) != S_OK) {
		return FailRet("CreateTexture2D( DS ) failed.");
	}

	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV = {};
	descDSV.Format = descTex2d.Format;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	hr = g_device->CreateDepthStencilView(texDS.Get(), &descDSV, g_viewDS.ReleaseAndGetAddressOf());
	texDS.Reset();

	return true;
}

bool myParcle::DoTest() 
{
	HRESULT hr = S_OK;

	PostRenderer quadPostRenderer;
	// TO DO ...

	D3D11_MAPPED_SUBRESOURCE mapRes;
	ZeroMemory(&mapRes, sizeof(D3D11_MAPPED_SUBRESOURCE));
	g_context->Map(g_myCB.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapRes);
	memcpy(mapRes.pData, &quadPostRenderer, sizeof(PostRenderer));
	g_context->Unmap(g_myCB.Get(), 0);


	UINT stride = sizeof(Vector3);
	UINT offset = 0;
	g_context->IASetInputLayout(g_inputLayerP.Get());
	g_context->IASetVertexBuffers(0, 1, g_testGeoVertexBuffer.GetAddressOf(), &stride,
		&offset);
	g_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	g_context->VSSetShader(g_myVS.Get(), nullptr, 0);
	g_context->VSSetConstantBuffers(0, 1, g_myCB.GetAddressOf());
	g_context->PSSetShader(g_myPS.Get(), nullptr, 0);

	g_context->OMSetDepthStencilState(g_myDSS.Get(), 1);
	g_context->RSSetState(g_myRS.Get());

	g_context->Draw(3, 0);

	g_context->Flush();


		return true;
	}

// rtRes.Get() != rtPrevRes.Get() 안 에 한 번만 들어 가는지...
bool myParcle::GetDX11SharedRenderTarget(ID3D11Device* dx11ImGuiDevice, ID3D11ShaderResourceView** sharedSRV, int& w, int& h)
{
	w = g_rt_w, h = g_rt_h;

	*sharedSRV = nullptr;

	if (g_device == NULL) {
		return FailRet("No GPU Manager is assigned!");
	}

	ComPtr<ID3D11Resource> rtRes;
	g_viewRT->GetResource(&rtRes);

	ComPtr<ID3D11Resource> rtPrevRes;

	if (g_sharedSRV)
		g_sharedSRV->GetResource(&rtPrevRes);

	if (rtRes.Get() != rtPrevRes.Get()) {
		g_sharedSRV.Reset();

		ComPtr<ID3D11Texture2D> rtTex2D((ID3D11Texture2D*)rtRes.Get());

		// QI IDXGIResource interface to synchronized shared surface.
		IDXGIResource* pDXGIResource = NULL;
		rtTex2D->QueryInterface(__uuidof(IDXGIResource), (LPVOID*)&pDXGIResource);

		// obtain handle to IDXGIResource object.
		HANDLE sharedHandle;
		// this code snippet is only for dx11.0
		// for dx11.1 or higher, refer to
		// https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiresource-getsharedhandle
		pDXGIResource->GetSharedHandle(&sharedHandle);
		pDXGIResource->Release();

		ID3D11Device* pdx11AnotherDev = dx11ImGuiDevice;

		if (sharedHandle == NULL) {
			return FailRet("Not Allowed for Shared Resource!");
		}

		ID3D11Texture2D* rtTex2;
		pdx11AnotherDev->OpenSharedResource(sharedHandle, __uuidof(ID3D11Texture2D), (LPVOID*)&rtTex2);

		// Create texture view
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		ZeroMemory(&srvDesc, sizeof(srvDesc));
		srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = 0;

		pdx11AnotherDev->CreateShaderResourceView(rtTex2, &srvDesc, &g_sharedSRV);
		rtTex2->Release();
	}

	*sharedSRV = g_sharedSRV.Get();

	return true;
}

void myParcle::DeinitEngine()
{
	g_myRS.Reset();
	g_myDSS.Reset();

	g_inputLayerPCN.Reset();

	g_testGeoVertexBuffer.Reset();
	g_myCB.Reset();

	g_myVS.Reset();
	g_myPS.Reset();

	g_sharedSRV.Reset();
	g_viewRT.Reset();
	g_viewDS.Reset();

	g_context.Reset();
	g_device.Reset();
}