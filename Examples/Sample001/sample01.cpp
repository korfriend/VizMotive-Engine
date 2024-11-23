// Filament highlevel APIs
#include "HighAPIs/VzEngineAPIs.h"

#include <iostream>
#include <windowsx.h>

#include <tchar.h>
#include <shellscalingapi.h>

#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/transform.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "glm/gtx/vector_angle.hpp"

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

HWND createNativeWindow(HINSTANCE hInstance, int nCmdShow, int width, int height) {
    const wchar_t CLASS_NAME[] = L"Sample Window Class";

    WNDCLASS wc = { };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0,                              // Optional window styles.
        CLASS_NAME,                     // Window class
        L"Learn to Program Windows",    // Window text
        WS_OVERLAPPEDWINDOW,            // Window style
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL,       // Parent window    
        NULL,       // Menu
        hInstance,  // Instance handle
        NULL        // Additional application data
    );

    if (hwnd == NULL) {
        std::cerr << "Failed to create window." << std::endl;
        return NULL;
    }

    ShowWindow(hwnd, nCmdShow);

    return hwnd;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // DPI 인식을 활성화
    EnableDpiAwareness();

    HWND hwnd = createNativeWindow(hInstance, nCmdShow, 800, 600);
    if (!hwnd) {
        return -1;
    }

    HDC hdc = GetDC(hwnd);
    if (!hdc) {
        std::cerr << "Failed to get device context." << std::endl;
        return -1;
    }

    RECT rc;
    GetClientRect(hwnd, &rc);
    uint32_t w = rc.right - rc.left;
    uint32_t h = rc.bottom - rc.top;
    float dpi = 96.f;

    vzm::ParamMap<std::string> arguments;
    //arguments.SetString("API", "DX11");
    arguments.SetString("GPU_VALIDATION", "VERBOSE");
    //arguments.SetParam("MAX_THREADS", 1u); // ~0u
    arguments.SetParam("MAX_THREADS", ~0u); // ~0u
    if (!vzm::InitEngineLib(arguments)) {
        std::cerr << "Failed to initialize engine library." << std::endl;
        return -1;
    }
    
    // Here, VID is same to the Entity that refers to the ECS-based components
    //  The different btw. VID and Entity is
    //  VID is an ID for node-based components (kind of abstraction)
    //  Entity is an ID for ECS-based components (internal implementation)
    
	vzm::VzScene* scene = vzm::NewScene("my scene");

    vzm::VzRenderer* renderer = vzm::NewRenderer("my renderer");
    renderer->SetCanvas(w, h, dpi, hwnd);
    renderer->SetClearColor({ 1.f, 1.f, 0.f, 1.f });
    //renderer->SetVisibleLayerMask(0x4, 0x4);
    
    vzm::VzCamera* cam = vzm::NewCamera("my camera");
	glm::fvec3 p(0, 0, 10);
	glm::fvec3 at(0, 0, -4);
	glm::fvec3 v(0, 0, -1);
	glm::fvec3 u(0, 1, 0);
    cam->SetWorldPose(__FC3 p, __FC3 at, __FC3 u);
    
    cam->SetPerspectiveProjection(0.1f, 1000.f, 45.f, (float)w / (float)h);
    
    vzm::VzActor* root_obj_actor = vzm::LoadModelFile("../Assets/obj_files/skull/12140_Skull_v3_L2.obj");
    root_obj_actor->SetScale({ 0.1f, 0.1f, 0.1f });
    
	vzm::VzGeometry* geometry_test = vzm::NewGeometry("my geometry");
	geometry_test->MakeTestQuadWithUVs();
	vzm::VzMaterial* material_test = vzm::NewMaterial("my material");
    material_test->SetShaderType(vzm::ShaderType::PBR);
    material_test->SetDoubleSided(true);

	vzm::VzGeometry* geometry_test2 = vzm::NewGeometry("my triangles");
	geometry_test2->MakeTestTriangle();

	vzm::VzTexture* texture = vzm::NewTexture("my texture");
    texture->LoadImageFile("../Assets/testimage_2ns.jpg");

    vzm::VzVolume* volume = vzm::NewVolume("my dicom volume");
	{
		vzm::ParamMap<std::string> io;
		io.SetParam("filename", std::string("d:/aaa.dcm"));
		io.SetParam("volume texture entity", volume->GetVID());
		vzm::ExecutePluginFunction("PluginSample001", "ImportDicom", io);
    }
    
    material_test->SetVolumeTexture(volume, vzm::VolumeTextureSlot::VOLUME_DENSITYMAP);
    material_test->SetTexture(texture, vzm::TextureSlot::BASECOLORMAP);

	vzm::VzActor* actor_test = vzm::NewActor("my actor", geometry_test, material_test);
	actor_test->SetScale({ 2.f, 2.f, 2.f });
	actor_test->SetPosition({ 0, 0, -1.f });

	vzm::VzActor* actor_test2 = vzm::NewActor("my actor2");
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

	vzm::VzMaterial* material_test3 = vzm::NewMaterial("my material 3");
	vzm::VzTexture* tex_otf_test3 = vzm::NewTexture("my material 3's OTF");
	std::vector<uint8_t> otf_array(1024 * 2 * 4);
	tex_otf_test3->LoadMemory("my otf 1", otf_array, vzm::TextureFormat::R8G8B8A8_UNORM, 1024, 2, 1);

	vzm::VzActor* actor_test3 = vzm::NewActor("my actor3", nullptr, material_test3);

	material_test3->SetVolumeTexture(volume, vzm::VolumeTextureSlot::VOLUME_DENSITYMAP);
	material_test3->SetLookupTable(tex_otf_test3, vzm::LookupTableSlot::LOOKUP_OTF);

	vzm::VzLight* light_test = vzm::NewLight("my light");
    light_test->SetLightIntensity(5.f);
    light_test->SetPosition({ 0.f, 0.f, 100.f });
    light_test->SetEulerAngleZXYInDegree({ 0, 180, 0 });

	vzm::AppendSceneCompTo(actor_test, scene);
	vzm::AppendSceneCompTo(actor_test2, scene);
	vzm::AppendSceneCompTo(actor_test3, scene);
	vzm::AppendSceneCompTo(light_test, scene);
	vzm::AppendSceneCompTo(root_obj_actor, scene);
    
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
            {
                done = true;
            }
        }

		if (done)
			break;
		static float x_rot = glm::radians<float>(2);
		p = glm::rotateX(p, x_rot);
		v = glm::rotateX(v, x_rot);
		u = glm::rotateX(u, x_rot);
		cam->SetWorldPose(__FC3 p, __FC3 v, __FC3 u);
		renderer->Render(scene, cam);
    }
    vzm::DeinitEngineLib();

    ReleaseDC(hwnd, hdc); 

    return 0;
}

// Main code
int main(int, char**)
{
    return wWinMain(GetModuleHandle(NULL), NULL, GetCommandLine(), SW_SHOWNORMAL);\
}

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    //vzm::VzRenderer* renderer = nullptr;
    //if (vzm::IsValidEngineLib())
    //{
    //    renderer = (vzm::VzRenderer*)vzm::GetFirstComponentByName("my renderer");
    //}
    switch (msg)
    {
    case WM_CLOSE:
    {
        break;
    }
    case WM_KEYDOWN:
        switch (wParam) {
        case 'R': vzm::ReloadShader(); 
            break;
        default:
            break;
		}
		return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    {
        break;
    }
    case WM_MOUSEMOVE:
    {
        break;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    {
        break;
    }
    case WM_MOUSEWHEEL:
    {
        break;
    }
    case WM_SIZE:
    {
        //if (renderer)
		//{
		//	RECT rc;
		//	GetClientRect(hWnd, &rc);
		//	UINT width = rc.right - rc.left;
		//	UINT height = rc.bottom - rc.top;
		//	renderer->ResizeCanvas(width, height);
		//	//vzm::VzBaseComp* scene = vzm::GetFirstComponentByName("my scene");
		//	//vzm::VzBaseComp* cam = vzm::GetFirstComponentByName("my camera");
        //    //renderer->Render(scene, cam);
        //}
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
