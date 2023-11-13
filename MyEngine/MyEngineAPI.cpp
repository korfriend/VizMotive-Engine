#include "MyEngineAPI.h"

#include <d3d11.h>

#include <wrl.h> 
#include <spdlog/spdlog.h>
#include <iostream>

using namespace Microsoft::WRL;
using namespace std;

std::shared_ptr<spdlog::logger> g_apiLogger;

static ComPtr<ID3D11Device> g_device;
static ComPtr<ID3D11DeviceContext> g_context;

//static ComPtr<ID3D11Texture2D> g_texRT;
//static ComPtr<ID3D11Texture2D> g_texDS;
static ComPtr<ID3D11RenderTargetView> g_viewRT;
static ComPtr<ID3D11DepthStencilView> g_viewDS;
static ComPtr<ID3D11RasterizerState> g_myRS;
static ComPtr<ID3D11DepthStencilState> g_myDSS;

static ComPtr<ID3D11Buffer> g_testGeoVertexBuffer;
static ComPtr<ID3D11Buffer> g_myCB;
static ComPtr<ID3D11VertexShader> g_myVS;
static ComPtr<ID3D11PixelShader> g_myPS;

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
		g_apiLogger->error("D3D11CreateDevice() failed.");
		return false;
	}

	//static ComPtr<ID3D11RasterizerState> g_myRS;
	//static ComPtr<ID3D11DepthStencilState> g_myDSS;

	return true;
}

bool myParcle::SetRenderTargetSize(int w, int h)
{
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
		g_apiLogger->error("CreateTexture2D() failed.");
		return false;
	}

	D3D11_RENDER_TARGET_VIEW_DESC descRTV = {};
	descRTV.Format = descTex2d.Format;
	descRTV.Texture2D.MipSlice = 0;
	descRTV.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
	HRESULT hr = g_device->CreateRenderTargetView(texRT.Get(), &descRTV, &g_viewRT);
	texRT.Reset();
	ComPtr<ID3D11Resource> tt;
	g_viewRT->GetResource(&tt);

	ComPtr<ID3D11Texture2D> tt2((ID3D11Texture2D*)tt.Get());

	if (hr != S_OK) {
		g_apiLogger->error("CreateRenderTargetView() failed.");
		return false;
	}
	
	ComPtr<ID3D11Texture2D> texDS;
	//static ComPtr<ID3D11RenderTargetView> g_viewRT;
	//static ComPtr<ID3D11DepthStencilView> g_viewDS;

	g_viewDS.Reset();

	return true;
}

bool myParcle::DoTest()
{

	return true;
}

bool myParcle::GetRenderTarget()
{
	return true;
}

void myParcle::DeinitEngine()
{
	g_viewRT.Reset();

	g_context.Reset();
	g_device.Reset();
}