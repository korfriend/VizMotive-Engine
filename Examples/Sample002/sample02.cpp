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
	arguments.SetParam("MAX_THREADS", 1u);
    if (!vzm::InitEngineLib(arguments)) {
        std::cerr << "Failed to initialize engine library." << std::endl;
        return -1;
    }

    // Here, VID is same to the Entity that refers to the ECS-based components
    //  The different btw. VID and Entity is
    //  VID is an ID for node-based components (kind of abstraction)
    //  Entity is an ID for ECS-based components (internal implementation)

	vzm::VzScene* scene = vzm::NewScene("my scene");
    //scene->LoadIBL("../../../VisualStudio/samples/assets/ibl/lightroom_14b");
    //
    //vzm::VzActor* actor = vzm::LoadTestModelIntoActor("my test model");
    //glm::fvec3 euler(3.14 / 10, 3.14 / 7, 3.14 / 5);
    //actor->SetRotation(__FP euler);
    //std::vector<vzm::VzActor*> loaded_actors;
    //vzm::VzActor* actor_axis = vzm::LoadModelFileIntoActors("../assets/xyz.obj", loaded_actors);
    //glm::fvec3 scale(3.f);
    //actor_axis->SetScale(__FP scale);
    //
    vzm::VzRenderer* renderer = vzm::NewRenderer("my renderer");
    renderer->SetCanvas(w, h, dpi, hwnd);
    renderer->SetClearColor({ 1.f, 1.f, 0.f, 1.f });
    //renderer->SetVisibleLayerMask(0x4, 0x4);
    //
    vzm::VzCamera* cam = vzm::NewCamera("my camera");
    glm::fvec3 p(0, 0, 100);
    glm::fvec3 at(0, 0, -4);
	glm::fvec3 u(0, 1, 0);
    cam->SetWorldPose(__FC3 p, __FC3 at, __FC3 u);
    cam->SetPerspectiveProjection(0.1f, 1000.f, 45.f, (float)w / (float)h);

    ActorVID root_actor_vid = vzm::LoadModelFile("../Assets/obj_files/skull/12140_Skull_v3_L2.obj");

	vzm::VzGeometry* geometry_test = vzm::NewGeometry("my geometry");
	//geometry_test->MakeTestTriangle();
	geometry_test->MakeTestQuadWithUVs();
	vzm::VzMaterial* material_test = vzm::NewMaterial("my material");
    material_test->SetShaderType(vzm::ShaderType::PBR);
    material_test->SetDoubleSided(true);

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
	//actor_test->SetGeometry(geometry_test);
    //actor_test->SetMaterial(material_test, 0);

	vzm::VzLight* light_test = vzm::NewLight("my light");
    light_test->SetPosition({ 0, 0, 100 });
    light_test->SetEulerAngleZXYInDegree({ 0, 180, 0 });


    // Actor New
    // Geometry New
    // Actor to Scene
    // Scene Update (Geometry to GPU)
    // Graphics Pipeline
    // Material ...
	// Draw it

	vzm::AppendSceneCompTo(actor_test, scene);
	vzm::AppendSceneCompTo(light_test, scene);
	vzm::AppendSceneCompVidTo(root_actor_vid, scene->GetVID());

	//vzm::AppendSceneCompTo(actor_axis, actor);
	//vzm::AppendSceneCompTo(light, scene);
	//vzm::AppendSceneCompTo(cam, scene);

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
		static float x_rot = 1.f / 180.f * 3.14f;
		p = glm::rotateX(p, x_rot);
		at = glm::rotateX(at, x_rot);
		u = glm::rotateX(u, x_rot);
		//x_rot += 1.f / 180.f * 3.14f;
		cam->SetWorldPose(__FC3 p, __FC3 at, __FC3 u);
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
    //VID vid_scene = vzm::GetFirstVidByName("my scene");
    //VID vid_renderer = vzm::GetFirstVidByName("my renderer");
    //VID vid_camera = vzm::GetFirstVidByName("my camera");
    //vzm::VzScene* scene = (vzm::VzScene*)vzm::GetVzComponent(vid_scene);
    //vzm::VzRenderer* renderer = (vzm::VzRenderer*)vzm::GetVzComponent(vid_renderer);
    //vzm::VzCamera* camera = (vzm::VzCamera*)vzm::GetVzComponent(vid_camera);
    //vzm::VzCamera::Controller* cc = nullptr;
    //bool is_valid = false;
    //uint32_t w = 0, h = 0;
    //if (camera && renderer)
    //{
    //    cc = camera->GetController();
    //    renderer->GetCanvas(&w, &h, nullptr);
    //    is_valid = w > 0 && h > 0;
    //}

    switch (msg)
    {
    case WM_CLOSE:
    {
        //vzm::RemoveComponent(vid_camera);
        //vzm::ReleaseWindowHandlerTasks(hWnd);
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
		//if (is_valid) {
		//    int x = GET_X_LPARAM(lParam);
		//    int y = h - GET_Y_LPARAM(lParam);
		//    //glm::fvec3 p, v, u;
		//    //camera->GetWorldPose(__FC3 p, __FC3 v, __FC3 u);
		//    //*(glm::fvec3*)cc->orbitHomePosition = p;
		//    //cc->UpdateControllerSettings();
		//    cc->GrabBegin(x, y, msg == WM_RBUTTONDOWN);
		//}
        break;
    }
    case WM_MOUSEMOVE:
    {
        //if (is_valid) {
        //    int x = GET_X_LPARAM(lParam);
        //    int y = h - GET_Y_LPARAM(lParam);
        //    cc->GrabDrag(x, y);
        //}
        break;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    {
        //if (cc) {
        //    cc->GrabEnd();
        //}
        break;
    }
    case WM_MOUSEWHEEL:
    {
        //if (is_valid) {
        //    int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        //    int x = GET_X_LPARAM(lParam);
        //    int y = h - GET_Y_LPARAM(lParam);
        //    cc->Scroll(x, y, -(float)zDelta);
        //}
        break;
    }
    case WM_SIZE:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);
        //UINT width = rc.right - rc.left;
        //UINT height = rc.bottom - rc.top;
        //if (is_valid)
        //{
        //    cc->SetViewport(w, h);
        //    renderer->SetCanvas(width, height, 96.f, hWnd);
        //    float zNearP, zFarP, fovInDegree;
        //    camera->GetPerspectiveProjection(&zNearP, &zFarP, &fovInDegree, nullptr);
        //    camera->SetPerspectiveProjection(zNearP, zFarP, fovInDegree, (float)w / (float)h);
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
