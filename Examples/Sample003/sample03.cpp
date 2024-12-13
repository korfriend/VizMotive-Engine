// Filament highlevel APIs
#include "HighAPIs/VzEngineAPIs.h"
#include "Utils/Backlog.h"
#include "Utils/EventHandler.h"

#include <iostream>
#include <windowsx.h>

// imgui
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include <d3d12.h>
#include <dxgi1_4.h>

#include <tchar.h>
#include <shellscalingapi.h>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/transform.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/vector_angle.hpp"

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

/*
// Windows 8.1 및 Windows 10에서 DPI 인식을 설정하는 코드
void EnableDpiAwareness() {
    // Windows 10에서 사용할 수 있는 DPI 인식 설정
    HMODULE hUser32 = LoadLibrary(TEXT("user32.dll"));
    if (hUser32) {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
        SetProcessDpiAwarenessContextProc SetProcessDpiAwarenessContextFunc =
            (SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");

        if (SetProcessDpiAwarenessContextFunc) {
            SetProcessDpiAwarenessContextFunc(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        else {
            // Windows 8.1에서 사용할 수 있는 DPI 인식 설정
            HMODULE hShcore = LoadLibrary(TEXT("shcore.dll"));
            if (hShcore) {
                typedef HRESULT(WINAPI* SetProcessDpiAwarenessProc)(PROCESS_DPI_AWARENESS);
                SetProcessDpiAwarenessProc SetProcessDpiAwarenessFunc =
                    (SetProcessDpiAwarenessProc)GetProcAddress(hShcore, "SetProcessDpiAwareness");

                if (SetProcessDpiAwarenessFunc) {
                    SetProcessDpiAwarenessFunc(PROCESS_PER_MONITOR_DPI_AWARE);
                }
                FreeLibrary(hShcore);
            }
        }
        FreeLibrary(hUser32);
    }
}
/**/
struct FrameContext
{
	ID3D12CommandAllocator* CommandAllocator;
	UINT64                  FenceValue;
};

// Data
static int const                    NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext                 g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_frameIndex = 0;

static int const                    NUM_BACK_BUFFERS = 3;
static ID3D12Device* g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
static ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;
static ID3D12Fence* g_fence = nullptr;
static HANDLE                       g_fenceEvent = nullptr;
static UINT64                       g_fenceLastSignaledValue = 0;
static IDXGISwapChain3* g_pSwapChain = nullptr;
static HANDLE                       g_hSwapChainWaitableObject = nullptr;
static ID3D12Resource* g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE  g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForLastSubmittedFrame();
FrameContext* WaitForNextFrameResources();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Windows 8.1 및 Windows 10에서 DPI 인식을 설정하는 코드
void EnableDpiAwareness() {
	// Windows 10에서 사용할 수 있는 DPI 인식 설정
	HMODULE hUser32 = LoadLibrary(TEXT("user32.dll"));
	if (hUser32) {
		typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
		SetProcessDpiAwarenessContextProc SetProcessDpiAwarenessContextFunc =
			(SetProcessDpiAwarenessContextProc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");

		if (SetProcessDpiAwarenessContextFunc) {
			SetProcessDpiAwarenessContextFunc(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		}
		else {
			// Windows 8.1에서 사용할 수 있는 DPI 인식 설정
			HMODULE hShcore = LoadLibrary(TEXT("shcore.dll"));
			if (hShcore) {
				typedef HRESULT(WINAPI* SetProcessDpiAwarenessProc)(PROCESS_DPI_AWARENESS);
				SetProcessDpiAwarenessProc SetProcessDpiAwarenessFunc =
					(SetProcessDpiAwarenessProc)GetProcAddress(hShcore, "SetProcessDpiAwareness");

				if (SetProcessDpiAwarenessFunc) {
					SetProcessDpiAwarenessFunc(PROCESS_PER_MONITOR_DPI_AWARE);
				}
				FreeLibrary(hShcore);
			}
		}
		FreeLibrary(hUser32);
	}
}

// Main code
DXGI_SWAP_CHAIN_DESC1 sd;

int main(int, char**)
{
	// DPI 인식을 활성화
	EnableDpiAwareness();

	// Create application window
	//ImGui_ImplWin32_EnableDpiAwareness();
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
	::RegisterClassExW(&wc);
	HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX12 Example", WS_OVERLAPPEDWINDOW, 30, 30, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

	vzm::ParamMap<std::string> arguments;
	//arguments.SetString("API", "DX11");
	arguments.SetString("GPU_VALIDATION", "VERBOSE");
	//arguments.SetParam("MAX_THREADS", 1u); // ~0u
	arguments.SetParam("MAX_THREADS", ~0u); // ~0u
	if (!vzm::InitEngineLib(arguments)) {
		std::cerr << "Failed to initialize engine library." << std::endl;
		return -1;
	}

	// Initialize Direct3D
	if (!CreateDeviceD3D(hwnd)) // calls pdx12Debug->EnableDebugLayer();
	{
		CleanupDeviceD3D();
		::UnregisterClassW(wc.lpszClassName, wc.hInstance);
		return 1;
	}

	// Show the window
	::ShowWindow(hwnd, SW_SHOWDEFAULT);
	::UpdateWindow(hwnd);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(g_pd3dDevice, NUM_FRAMES_IN_FLIGHT,
		sd.Format, g_pd3dSrvDescHeap, //DXGI_FORMAT_R11G11B10_FLOAT, R10G10B10A2_UNORM
		g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != nullptr);

	using namespace vzm;
	VzScene* scene = nullptr;
	VzCamera* camera = nullptr;
	VzRenderer* renderer = nullptr;
	ImVec2 wh(512, 512);
	{
		scene = NewScene("my scene");

		VzLight* light = NewLight("my light");
		light->SetLightIntensity(5.f);
		light->SetPosition({ 0.f, 0.f, 100.f });
		light->SetEulerAngleZXYInDegree({ 0, 180, 0 });
		scene->AppendChild(light);

		renderer = NewRenderer("my renderer");
		renderer->SetCanvas(1, 1, 96.f, nullptr);
		renderer->SetClearColor({ 1.f, 1.f, 0.f, 1.f });

		camera = NewCamera("my camera");
		glm::fvec3 pos(0, 0, 5), up(0, 1, 0), at(0, 0, -4);
		glm::fvec3 view = at - pos;
		camera->SetWorldPose(__FC3 pos, __FC3 view, __FC3 up);
		camera->SetPerspectiveProjection(0.1f, 100.f, 45.f, 1.f);

		vzm::VzActor* axis_helper = vzm::LoadModelFile("../Assets/axis.obj");
		scene->AppendChild(axis_helper);

		vzm::VzGeometry* geometry_test = vzm::NewGeometry("my geometry");
		geometry_test->MakeTestQuadWithUVs();
		vzm::VzMaterial* material_test = vzm::NewMaterial("my material");
		material_test->SetShaderType(vzm::ShaderType::PBR);
		material_test->SetDoubleSided(true);

		vzm::VzGeometry* geometry_test2 = vzm::NewGeometry("my triangles");
		geometry_test2->MakeTestTriangle();

		vzm::VzTexture* texture = vzm::NewTexture("my texture");
		texture->CreateTextureFromImageFile("../Assets/testimage_2ns.jpg");
		material_test->SetTexture(texture, vzm::TextureSlot::BASECOLORMAP);

		vzm::VzActor* actor_test = vzm::NewActor("my actor", geometry_test, material_test);
		scene->AppendChild(actor_test);
		actor_test->SetScale({ 2.f, 2.f, 2.f });
		actor_test->SetPosition({ 0, 0, -1.f });

		vzm::VzActor* actor_test2 = vzm::NewActor("my actor2");
		scene->AppendChild(actor_test2);
		actor_test2->SetGeometry(geometry_test2);
		actor_test2->SetPosition({ 0, -2, 0 });
		vfloat4 colors[3] = { {1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1} };
		for (size_t i = 0, n = geometry_test2->GetNumParts(); i < n; ++i)
		{
			vzm::VzMaterial* material = vzm::NewMaterial("my test2's material " + i);
			actor_test2->SetMaterial(material, i);
			material->SetShaderType(vzm::ShaderType::PBR);
			material->SetDoubleSided(true);
			material->SetBaseColor(colors[i]);
		}

		vzm::VzMaterial* material_volume = vzm::NewMaterial("volume material");
		vzm::VzTexture* otf_volume = vzm::NewTexture("volume material's OTF");
		const uint32_t otf_w = 256;
		std::vector<uint8_t> otf_array(otf_w * 4 * 3);
		for (size_t i = 0; i < otf_w; i++)
		{
			otf_array[(otf_w * 4 * 0) + 4 * i + 0] = 255;
			otf_array[(otf_w * 4 * 0) + 4 * i + 1] = 0;
			otf_array[(otf_w * 4 * 0) + 4 * i + 2] = 0;
			otf_array[(otf_w * 4 * 0) + 4 * i + 3] = i < 180 ? 0 :
				i < 210 ? (uint8_t)((float)(i - 180) / 30.f * 255.f) : 255;

			otf_array[(otf_w * 4 * 1) + 4 * i + 0] = 0;
			otf_array[(otf_w * 4 * 1) + 4 * i + 1] = 255;
			otf_array[(otf_w * 4 * 1) + 4 * i + 2] = 0;
			otf_array[(otf_w * 4 * 1) + 4 * i + 3] = i < 100 ? 0 :
				i < 200 ? (uint8_t)((float)(i - 100) / 100.f * 255.f) : 255;

			otf_array[(otf_w * 4 * 2) + 4 * i + 0] = 0;
			otf_array[(otf_w * 4 * 2) + 4 * i + 1] = 0;
			otf_array[(otf_w * 4 * 2) + 4 * i + 2] = 255;
			otf_array[(otf_w * 4 * 2) + 4 * i + 3] = i < 100 ? 0 :
				i < 130 ? (uint8_t)((float)(i - 100) / 30.f * 255.f) : 255;

		}
		otf_volume->CreateLookupTexture("volume otf", otf_array, vzm::TextureFormat::R8G8B8A8_UNORM, otf_w, 3, 1);
		otf_volume->UpdateLookup(otf_array, 180, 255);

		vzm::VzActor* volume_actor = vzm::NewActor("my volume actor", nullptr, material_volume);
		scene->AppendChild(volume_actor);

		vzm::VzVolume* volume = vzm::NewVolume("my dicom volume");
		{
			vzm::ParamMap<std::string> io;
			io.SetParam("filename", std::string("d:/aaa.dcm"));
			io.SetParam("volume texture entity", volume->GetVID());
			vzm::ExecutePluginFunction("PluginSample001", "ImportDicom", io);
		}
		material_volume->SetVolumeTexture(volume, vzm::VolumeTextureSlot::VOLUME_DENSITYMAP);
		material_volume->SetLookupTable(otf_volume, vzm::LookupTableSlot::LOOKUP_OTF);


		VzArchive* archive = vzm::NewArchive("test archive");
		archive->Store(camera);
	}

	// Our state
	bool show_demo_window = true;
	bool show_another_window = false;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	// Main loop
	bool done = false;
	while (!done)
	{
		// Poll and handle messages (inputs, window resize, etc.)
		// See the WndProc() function below for our to dispatch events to the Win32 backend.
		MSG msg;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
		{
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				done = true;
		}
		if (done)
			break;

		// Start the Dear ImGui frame
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		using namespace vzm;
		{
			ImGui::Begin("3D Viewer");
			{
				static ImVec2 prevWindowSize = ImVec2(0, 0);
				ImVec2 curWindowSize = ImGui::GetWindowSize();

				if (prevWindowSize.x * prevWindowSize.y == 0)
					ImGui::SetWindowSize(ImVec2(0, 0));

				bool resized = prevWindowSize.x != curWindowSize.x || prevWindowSize.y != curWindowSize.y;
				prevWindowSize = curWindowSize;

				if (resized)
				{
					ImVec2 canvas_size = ImGui::GetContentRegionAvail();
					canvas_size.y = std::max(canvas_size.y, 1.f);
					renderer->ResizeCanvas(canvas_size.x, canvas_size.y, camera->GetVID());
					wh = canvas_size;
				}
				ImVec2 win_pos = ImGui::GetWindowPos();
				ImVec2 cur_item_pos = ImGui::GetCursorPos();
				ImGui::InvisibleButton("render window", wh, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				ImGui::SetItemAllowOverlap();

				bool is_hovered = ImGui::IsItemHovered(); // Hovered
				bool is_active = ImGui::IsItemActive();   // Held

				if (is_hovered && !resized)
				{
					static glm::fvec2 prevMousePos(0);
					glm::fvec2 ioPos = *(glm::fvec2*)&io.MousePos;
					glm::fvec2 s_pos = *(glm::fvec2*)&cur_item_pos;
					glm::fvec2 w_pos = *(glm::fvec2*)&win_pos;
					glm::fvec2 m_pos = ioPos - s_pos - w_pos;
					glm::fvec2 pos_ss = m_pos;

					OrbitalControl* orbit_control = camera->GetOrbitControl();
					orbit_control->Initialize(renderer->GetVID(), {0, 0, 0}, 2.f);

					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						float np, fp;
						camera->GetPerspectiveProjection(&np, &fp, NULL, NULL);
						orbit_control->Start(__FC2 pos_ss);
					}
					else if ((ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.f))
						&& glm::length2(prevMousePos - m_pos) > 0)
					{
						if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
							orbit_control->PanMove(__FC2 pos_ss);
						else
							orbit_control->Move(__FC2 pos_ss);
					}
					else if (io.MouseWheel != 0)
					{
						glm::fvec3 pos, view, up;
						camera->GetWorldPose(__FC3 pos, __FC3 view, __FC3 up);
						if (io.MouseWheel > 0)
							pos += 0.2f * view;
						else
							pos -= 0.2f * view;
						camera->SetWorldPose(__FC3 pos, __FC3 view, __FC3 up);
					}
					prevMousePos = pos_ss;
				}

				ImGui::SetCursorPos(cur_item_pos);

				renderer->Render(scene, camera);

				uint32_t w, h;
				VzRenderer::SharedResourceTarget srt;
				renderer->GetSharedRenderTarget(g_pd3dDevice, g_pd3dSrvDescHeap, 1, srt , &w, &h);
				ImTextureID texId = (ImTextureID)srt.descriptorHandle;
				// https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
				ImGui::Image(texId, ImVec2((float)w, (float)h));
			}

			ImGui::End();

			ImGui::Begin("Controls");
			{
				if (ImGui::Button("Shader Reload"))
				{
					vzm::ReloadShader();
				}

				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
			}
			ImGui::End();

		}

		// Rendering
		ImGui::Render();

		FrameContext* frameCtx = WaitForNextFrameResources();
		UINT backBufferIdx = g_pSwapChain->GetCurrentBackBufferIndex();
		frameCtx->CommandAllocator->Reset();

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = g_mainRenderTargetResource[backBufferIdx];
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
		g_pd3dCommandList->ResourceBarrier(1, &barrier);

		// Render Dear ImGui graphics
		const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
		g_pd3dCommandList->ClearRenderTargetView(g_mainRenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
		g_pd3dCommandList->OMSetRenderTargets(1, &g_mainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
		g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		g_pd3dCommandList->ResourceBarrier(1, &barrier);
		g_pd3dCommandList->Close();

		g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);

		g_pSwapChain->Present(1, 0); // Present with vsync
		//g_pSwapChain->Present(0, 0); // Present without vsync

		UINT64 fenceValue = g_fenceLastSignaledValue + 1;
		g_pd3dCommandQueue->Signal(g_fence, fenceValue);
		g_fenceLastSignaledValue = fenceValue;
		frameCtx->FenceValue = fenceValue;
	}

	vzm::DeinitEngineLib();

	WaitForLastSubmittedFrame();

	// Cleanup
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(hwnd);
	::UnregisterClassW(wc.lpszClassName, wc.hInstance);

	return 0;
}

// Helper functions
bool CreateDeviceD3D(HWND hWnd)
{
	// Setup swap chain
	{
		ZeroMemory(&sd, sizeof(sd));
		sd.BufferCount = NUM_BACK_BUFFERS;
		sd.Width = 0;
		sd.Height = 0;
		sd.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
		sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.SampleDesc.Count = 1;
		sd.SampleDesc.Quality = 0;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		sd.Scaling = DXGI_SCALING_STRETCH;
		sd.Stereo = FALSE;
	}

	// [DEBUG] Enable debug interface
#ifdef DX12_ENABLE_DEBUG_LAYER
	ID3D12Debug* pdx12Debug = nullptr;
	// note : only one debug_layer is available
	//if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
	//	pdx12Debug->EnableDebugLayer();
#endif

	// Create device
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_2;
	if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
		return false;

	// [DEBUG] Setup debug interface to break on any warnings/errors
#ifdef DX12_ENABLE_DEBUG_LAYER
	if (pdx12Debug != nullptr)
	{
		ID3D12InfoQueue* pInfoQueue = nullptr;
		g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
		pInfoQueue->Release();
		pdx12Debug->Release();
	}
#endif

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		desc.NumDescriptors = NUM_BACK_BUFFERS;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		desc.NodeMask = 1;
		if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
			return false;

		SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
		for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		{
			g_mainRenderTargetDescriptor[i] = rtvHandle;
			rtvHandle.ptr += rtvDescriptorSize;
		}
	}

	{
		D3D12_DESCRIPTOR_HEAP_DESC desc = {};
		desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		desc.NumDescriptors = 4;
		desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
			return false;
	}

	{
		D3D12_COMMAND_QUEUE_DESC desc = {};
		desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		desc.NodeMask = 1;
		if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
			return false;
	}

	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
			return false;

	if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
		g_pd3dCommandList->Close() != S_OK)
		return false;

	if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
		return false;

	g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (g_fenceEvent == nullptr)
		return false;

	{
		IDXGIFactory4* dxgiFactory = nullptr;
		IDXGISwapChain1* swapChain1 = nullptr;
		if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) != S_OK)
			return false;
		if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, hWnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
			return false;
		if (swapChain1->QueryInterface(IID_PPV_ARGS(&g_pSwapChain)) != S_OK)
			return false;
		swapChain1->Release();
		dxgiFactory->Release();
		g_pSwapChain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
		g_hSwapChainWaitableObject = g_pSwapChain->GetFrameLatencyWaitableObject();
	}

	CreateRenderTarget();
	return true;
}

void CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (g_pSwapChain) { g_pSwapChain->SetFullscreenState(false, nullptr); g_pSwapChain->Release(); g_pSwapChain = nullptr; }
	if (g_hSwapChainWaitableObject != nullptr) { CloseHandle(g_hSwapChainWaitableObject); }
	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (g_frameContext[i].CommandAllocator) { g_frameContext[i].CommandAllocator->Release(); g_frameContext[i].CommandAllocator = nullptr; }
	if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = nullptr; }
	if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
	if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
	if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }
	if (g_fence) { g_fence->Release(); g_fence = nullptr; }
	if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
	if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

#ifdef DX12_ENABLE_DEBUG_LAYER
	IDXGIDebug1* pDebug = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
	{
		pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
		pDebug->Release();
	}
#endif
}

void CreateRenderTarget()
{
	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
	{
		ID3D12Resource* pBackBuffer = nullptr;
		g_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
		g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, g_mainRenderTargetDescriptor[i]);
		g_mainRenderTargetResource[i] = pBackBuffer;
	}
}

void CleanupRenderTarget()
{
	WaitForLastSubmittedFrame();

	for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
		if (g_mainRenderTargetResource[i]) { g_mainRenderTargetResource[i]->Release(); g_mainRenderTargetResource[i] = nullptr; }
}

void WaitForLastSubmittedFrame()
{
	FrameContext* frameCtx = &g_frameContext[g_frameIndex % NUM_FRAMES_IN_FLIGHT];

	UINT64 fenceValue = frameCtx->FenceValue;
	if (fenceValue == 0)
		return; // No fence was signaled

	frameCtx->FenceValue = 0;
	if (g_fence->GetCompletedValue() >= fenceValue)
		return;

	g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
	WaitForSingleObject(g_fenceEvent, INFINITE);
}

FrameContext* WaitForNextFrameResources()
{
	UINT nextFrameIndex = g_frameIndex + 1;
	g_frameIndex = nextFrameIndex;

	HANDLE waitableObjects[] = { g_hSwapChainWaitableObject, nullptr };
	DWORD numWaitableObjects = 1;

	FrameContext* frameCtx = &g_frameContext[nextFrameIndex % NUM_FRAMES_IN_FLIGHT];
	UINT64 fenceValue = frameCtx->FenceValue;
	if (fenceValue != 0) // means no fence was signaled
	{
		frameCtx->FenceValue = 0;
		g_fence->SetEventOnCompletion(fenceValue, g_fenceEvent);
		waitableObjects[1] = g_fenceEvent;
		numWaitableObjects = 2;
	}

	WaitForMultipleObjects(numWaitableObjects, waitableObjects, TRUE, INFINITE);

	return frameCtx;
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	vzm::VzRenderer* renderer = nullptr;
	if (vzm::IsValidEngineLib())
	{
		renderer = (vzm::VzRenderer*)vzm::GetFirstComponentByName("my renderer");
	}

	switch (msg)
	{
	case WM_KEYDOWN:
		switch (wParam) {
		case 'R':
			//vz::eventhandler::FireEvent(1, 0);
			vzm::ReloadShader();
			break;
		case '0': renderer->ShowDebugBuffer("NONE");
			break;
		case '1': renderer->ShowDebugBuffer("PRIMITIVE_ID");
			break;
		case '2': renderer->ShowDebugBuffer("INSTANCE_ID");
			break;
		case '3': renderer->ShowDebugBuffer("LINEAR_DEPTH");
			break;
		case '4': renderer->ShowDebugBuffer("NO_POSTPROCESSING");
			break;
		case 'N':
		{
			using namespace vzm;
			VzArchive* archive = (VzArchive*)GetFirstComponentByName("test archive");
			VzCamera* camera = (VzCamera*)GetFirstComponentByName("my camera");
			archive->Store(camera);
		}
		break;
		case 'M': 
		{
			using namespace vzm;
			VzArchive* archive = (VzArchive*)GetFirstComponentByName("test archive");
			VzCamera* camera = (VzCamera*)GetFirstComponentByName("my camera");
			archive->Load(camera);
		}
			break;
		default:
			break;
		}
		return 0;
	case WM_SIZE:
		if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
		{
			WaitForLastSubmittedFrame();
			CleanupRenderTarget();
			HRESULT result = g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
			assert(SUCCEEDED(result) && "Failed to resize swapchain.");
			CreateRenderTarget();
		}
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;
	case WM_DESTROY:
		::PostQuitMessage(0);
		return 0;
	}
	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
