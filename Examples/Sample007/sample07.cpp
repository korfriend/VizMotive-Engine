// Filament highlevel APIs
#include "vzm2/VzEngineAPIs.h"
#include "vzm2/utils/Backlog.h"
#include "vzm2/utils/EventHandler.h"
#include "vzm2/utils/Profiler.h"
#include "vzm2/utils/JobSystem.h"
#include "vzm2/utils/Config.h"
#include "vzm2/utils/vzMath.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "imgui/vzImGuiHelpers.h"
#include "imgui/IconsMaterialDesign.h"
#include "imgui/device_manager_dx12.h"

#include <iostream>
#include <windowsx.h>

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

struct FrameContext
{
	ID3D12CommandAllocator* CommandAllocator;
	UINT64 FenceValue;
};

// Data
static int const NUM_FRAMES_IN_FLIGHT = 3;
static FrameContext g_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
static UINT g_frameIndex = 0;

static int const NUM_BACK_BUFFERS = 3;
static ID3D12Device* g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap* g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap* g_pd3dSrvDescHeap = nullptr;
static ID3D12CommandQueue* g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList* g_pd3dCommandList = nullptr;
static ID3D12Fence* g_fence = nullptr;
static HANDLE g_fenceEvent = nullptr;
static UINT64 g_fenceLastSignaledValue = 0;
static IDXGISwapChain3* g_pSwapChain = nullptr;
static HANDLE g_hSwapChainWaitableObject = nullptr;
static ID3D12Resource* g_mainRenderTargetResource[NUM_BACK_BUFFERS] = {};
static D3D12_CPU_DESCRIPTOR_HANDLE g_mainRenderTargetDescriptor[NUM_BACK_BUFFERS] = {};

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
void WaitForLastSubmittedFrame();
FrameContext* WaitForNextFrameResources();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
DXGI_SWAP_CHAIN_DESC1 sd;

const uint32_t otfW = 256;
static float curOtfValue = 180, curOtfValuePrev = 180;
static float curOtfBandWidth = 50.f;
static float curWindowBandWidth = 100.f, curWindowBandWidthPrev = 100.f;
static float curWindowCenter = 100.f, curWindowCenterPrev = 100.f;

inline float linearWindowing(float value, float windowCenter, float windowWidth) {
	// Calculate half window width
	float halfWidth = windowWidth / 2.0f;

	// Calculate window boundaries
	float lowerBound = windowCenter - halfWidth;
	float upperBound = windowCenter + halfWidth;

	// Apply windowing function
	if (value <= lowerBound) {
		return 0.0f;
	}
	else if (value >= upperBound) {
		return 1.0f;
	}
	// Linear interpolation between 0 and 1
	return (value - lowerBound) / windowWidth;
}

int main(int, char**)
{
	// Create application window
	ImGui_ImplWin32_EnableDpiAwareness();
	WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
	::RegisterClassExW(&wc);
	HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX12 Example", WS_OVERLAPPEDWINDOW, 10, 10, 1680, 1200, nullptr, nullptr, wc.hInstance, nullptr);

	vzm::ParamMap<std::string> arguments;
	if (!vzm::InitEngineLib(arguments))
	{
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
	ImGuiIO& io = ImGui::GetIO();

	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	// ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(g_pd3dDevice, NUM_FRAMES_IN_FLIGHT,
		sd.Format, g_pd3dSrvDescHeap, // DXGI_FORMAT_R11G11B10_FLOAT, R10G10B10A2_UNORM
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
	// io.Fonts->AddFontDefault();
	// io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
	// io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	// io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	// io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	// ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	// IM_ASSERT(font != nullptr);

	using namespace vzm;
	VzScene* scene = nullptr;
	VzCamera* camera = nullptr;
	VzSlicer* slicer_curved = nullptr;
	VzRenderer* renderer3D = nullptr;
	VzRenderer* renderer_curvedslicer = nullptr;

	{
		renderer3D = NewRenderer("my renderer");
		renderer3D->SetCanvas(1, 1, 96.f, nullptr);

		renderer_curvedslicer = NewRenderer("my curved slicer");
		renderer_curvedslicer->SetCanvas(1, 1, 96.f, nullptr);

		scene = NewScene("my scene");

		VzLight* light = NewLight("my light");
		light->SetIntensity(5.f);
		scene->AppendChild(light);

		// === camera ===
		camera = NewCamera("my camera");
		glm::fvec3 pos(0, 0, 25), up(0, 1, 0), at(0, 0, -4);
		glm::fvec3 view = at - pos;
		camera->SetWorldPose(__FC3 pos, __FC3 view, __FC3 up);
		camera->SetPerspectiveProjection(0.1f, 5000.f, 45.f, 1.f);

		slicer_curved = NewSlicer("my curved slicer", true);
		slicer_curved->SetCurvedPlaneUp({ 0, 0, 1 });
		slicer_curved->SetOrthogonalProjection(1, 1, 10);
		slicer_curved->SetHorizontalCurveControls({ {-5, -3, 0}, {0, 2, 0}, {3, -3, 0} }, 0.01);
		slicer_curved->SetCurvedPlaneHeight(5.f);
		{
			vzm::VzGeometry* geometry_cslicer_helper = vzm::NewGeometry("geometry helper for curved slicer");
			slicer_curved->MakeCurvedSlicerHelperGeometry(geometry_cslicer_helper->GetVID());
			vzm::VzMaterial* material_curvedslicer0 = vzm::NewMaterial("curved slicer helper material: Plane");
			material_curvedslicer0->SetDoubleSided(true);
			vzm::VzMaterial* material_curvedslicer1 = vzm::NewMaterial("curved slicer helper material: Lines");
			material_curvedslicer1->SetBaseColor({ 1, 1, 1, 1 });
			vzm::VzActorStaticMesh* actor_cslicer_helper = vzm::NewActorStaticMesh("actor: geometry helper for curved slicer", geometry_cslicer_helper->GetVID(), material_curvedslicer0->GetVID());
			//vzm::VzActorStaticMesh* actor_cslicer_helper = vzm::NewActorStaticMesh("actor: geometry helper for curved slicer", geometry_cslicer_helper->GetVID(), material_curvedslicer1->GetVID());
			actor_cslicer_helper->SetMaterial(material_curvedslicer1, 1);
			scene->AppendChild(actor_cslicer_helper);
			vzlog("actor_cslicer_helper: %d", actor_cslicer_helper->GetVID());
		}

		vzm::VzActor* axis_helper = vzm::LoadModelFile("../Assets/axis.obj");
		axis_helper->SetScale({ 3.f, 3.f, 3.f });
		axis_helper->EnablePickable(false, true);
		scene->AppendChild(axis_helper);
		
		VzArchive* archive = vzm::NewArchive("test archive");
		archive->Store(camera);

		vz::jobsystem::context ctx_stl_loader1;
		vz::jobsystem::Execute(ctx_stl_loader1, [scene](vz::jobsystem::JobArgs args) {

			VzGeometry* geometry = vzm::NewGeometry("TS3M3008S.stl");
			geometry->LoadGeometryFile("../Assets/stl_files/TS3M3008S.stl");
			geometry->EnableGPUBVH(true);
			vzm::VzMaterial* material_stl = vzm::NewMaterial("my stl's material");
			material_stl->SetShaderType(vzm::ShaderType::PBR);
			material_stl->SetDoubleSided(true);
			material_stl->SetBaseColor({ 1, 0, 0, 1 });
			vzm::VzActorStaticMesh* actor_test3 = vzm::NewActorStaticMesh("my actor3", geometry->GetVID(), material_stl->GetVID());
			actor_test3->SetScale({ 0.5f, 0.5f, 0.5f });
			actor_test3->SetPosition({ 5, 0, 0 });
			scene->AppendChild(actor_test3);

			vzm::VzMaterial* material_stl_A = vzm::NewMaterial("my stl's material_A");
			material_stl_A->SetBaseColor({ 1, 1, 0, 1 });
			vzm::VzActorStaticMesh* actor_test5 = vzm::NewActorStaticMesh("my actor5", geometry->GetVID(), material_stl_A->GetVID());
			actor_test5->SetScale({ 0.5f, 0.5f, 0.5f });
			actor_test5->SetPosition({ -5, 0, 0 });
			actor_test5->SetRotateAxis({ 0, 1, 0 }, 90.f);
			scene->AppendChild(actor_test5);
			});

		vz::jobsystem::context ctx_stl_loader2;
		vz::jobsystem::Execute(ctx_stl_loader2, [scene](vz::jobsystem::JobArgs args) {

			VzGeometry* geometry = vzm::NewGeometry("PreparationScan.stl");
			geometry->LoadGeometryFile("../Assets/stl_files/PreparationScan.stl");
			vzm::VzMaterial* material_stl = vzm::NewMaterial("my stl's material 2");
			material_stl->SetShaderType(vzm::ShaderType::PBR);
			material_stl->SetDoubleSided(true);
			material_stl->SetBaseColor({ 0, 1, 1, 1 });
			vzm::VzActorStaticMesh* actor_test4 = vzm::NewActorStaticMesh("my actor4", geometry->GetVID(), material_stl->GetVID());
			actor_test4->SetScale({ 0.2f, 0.2f, 0.2f });
			actor_test4->EnableSlicerSolidFill(false);
			actor_test4->SetRotateAxis({ 0, 0, 1 }, 45.f);
			geometry->EnableGPUBVH(true);
			scene->AppendChild(actor_test4);

			});

		vz::jobsystem::context ctx_vol_loader;
		vz::jobsystem::Execute(ctx_vol_loader, [scene, slicer_curved](vz::jobsystem::JobArgs args) {

			vzm::VzVolume* volume = vzm::NewVolume("my dicom volume");
			{
				vzm::ParamMap<std::string> io;
				io.SetParam("filename", std::string("d:/aaa.dcm"));
				io.SetParam("volume texture entity", volume->GetVID());
				vzm::ExecutePluginFunction("PluginSample001", "ImportDicom", io);
			}

			vzm::VzTexture* otf_volume = vzm::NewTexture("volume material's OTF");
			std::vector<uint8_t> otf_array(otfW * 4 * 1);
			for (size_t i = 0; i < otfW; i++)
			{
				otf_array[(otfW * 4 * 0) + 4 * i + 0] = 255;
				otf_array[(otfW * 4 * 0) + 4 * i + 1] = 0;
				otf_array[(otfW * 4 * 0) + 4 * i + 2] = 0;
				otf_array[(otfW * 4 * 0) + 4 * i + 3] = i < 180 ? 0 :
					i < 210 ? (uint8_t)((float)(i - 180) / curOtfBandWidth * 255.f) : 255;

			}
			otf_volume->CreateLookupTexture("volume otf", otf_array, vzm::TextureFormat::R8G8B8A8_UNORM, otfW, 1, 1);
			otf_volume->UpdateLookup(otf_array, 180, 255);

			vzm::VzTexture* windowing_volume = vzm::NewTexture("volume material's windowing");
			for (size_t i = 0; i < otfW; i++)
			{
				otf_array[4 * i + 0] = 255;
				otf_array[4 * i + 1] = 255;
				otf_array[4 * i + 2] = 255;
				otf_array[4 * i + 3] = std::max(std::min((uint)(linearWindowing(i, curWindowCenter, curWindowBandWidth) * 255.f), 255u), 0u);
			}
			windowing_volume->CreateLookupTexture("windowing otf", otf_array, vzm::TextureFormat::R8G8B8A8_UNORM, otfW, 1, 1);
			windowing_volume->UpdateLookup(otf_array, curWindowCenter - curWindowBandWidth * 0.5f, curWindowCenter + curWindowBandWidth * 0.5f);

			vzm::VzMaterial* material_volume = vzm::NewMaterial("volume material");
			material_volume->SetVolumeTexture(volume, vzm::VolumeTextureSlot::VOLUME_DENSITYMAP);
			material_volume->SetLookupTable(otf_volume, vzm::LookupTableSlot::LOOKUP_OTF);
			material_volume->SetLookupTable(windowing_volume, vzm::LookupTableSlot::LOOKUP_WINDOWING);

			slicer_curved->SetDVRLookupSlot(LookupTableSlot::LOOKUP_WINDOWING);
			slicer_curved->SetDVRType(DVR_TYPE::XRAY_AVERAGE);

			vzm::VzActorVolume* volume_actor = vzm::NewActorVolume("my volume actor", material_volume->GetVID());
			volume_actor->SetScale({ 3, 3, 3 });
			scene->AppendChild(volume_actor);

			vzm::VzMaterial* material_curvedslicer0 = (vzm::VzMaterial*)vzm::GetFirstComponentByName("curved slicer helper material: Plane");
			material_curvedslicer0->SetShaderType(vzm::ShaderType::VOLUMEMAP);
			material_curvedslicer0->SetVolumeMapper(volume_actor->GetVID(), VolumeTextureSlot::VOLUME_DENSITYMAP, LookupTableSlot::LOOKUP_WINDOWING);
			material_curvedslicer0->SetVolumeTexture(volume->GetVID(), VolumeTextureSlot::VOLUME_DENSITYMAP);
			material_curvedslicer0->SetLookupTable(windowing_volume->GetVID(), LookupTableSlot::LOOKUP_WINDOWING);

			});

	}

	// Our state
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	ImGuizmo::Enable(true);
	
	vzimgui::VzImGuiFontManager font_manager;
	font_manager.AddFont("../imgui/Roboto-Medium.ttf");

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

		ImGuizmo::SetGizmoSizeClipSpace(0.12f);
		ImGui::PushFont(font_manager.GetCustomLargeFont());

		using namespace vzm;
		{
			static bool use_renderchain = true;
			vzm::PendingSubmitCommand(!use_renderchain);
			std::vector<ChainUnitRCam> render_chain;

			static VID highlighed_vid = INVALID_VID;
			static vzimgui::VzImGuizmo view3D_gizmo;
			static int belongto = -1;

			ImGui::Begin("3D Viewer");
			{
				static ImVec2 canvas_size_prev = ImVec2(0, 0);
				ImVec2 canvas_size = ImGui::GetContentRegionAvail();

				if (canvas_size_prev.x * canvas_size_prev.y == 0)
				{
					ImGui::SetWindowSize(ImVec2(0, 0));
					canvas_size.x = std::max(canvas_size.x, 1.f);
					canvas_size.y = std::max(canvas_size.y, 1.f);
				}

				bool resized = canvas_size_prev.x != canvas_size.x || canvas_size_prev.y != canvas_size.y;
				canvas_size_prev = canvas_size;

				if (resized)
				{
					renderer3D->ResizeCanvas((uint)canvas_size.x, (uint)canvas_size.y, camera->GetVID());
				}

				ImVec2 win_pos = ImGui::GetWindowPos();
				ImVec2 cur_item_pos = ImGui::GetCursorPos();
				ImGui::InvisibleButton("render window", canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				ImGui::SetItemAllowOverlap();

				bool is_hovered = ImGui::IsItemHovered(); // Hovered

				static bool mouse_control_flag = false;
				if (is_hovered && !resized && (!ImGuizmo::IsOver() || mouse_control_flag))
				{
					static glm::fvec2 prevMousePos(0);
					glm::fvec2 ioPos = *(glm::fvec2*)&io.MousePos;
					glm::fvec2 s_pos = *(glm::fvec2*)&cur_item_pos;
					glm::fvec2 w_pos = *(glm::fvec2*)&win_pos;
					glm::fvec2 m_pos = ioPos - s_pos - w_pos;
					glm::fvec2 pos_ss = m_pos;

					OrbitalControl* orbit_control = camera->GetOrbitControl();
					orbit_control->Initialize(renderer3D->GetVID(), { 0, 0, 0 }, 2.f);

					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
						{
							vfloat3 pos_picked;
							ActorVID actor_vid;
							if (renderer3D->Picking(scene, camera, *(vfloat2*)&pos_ss,
								vzm::VzRenderer::ActorFilter::MESH_OPAQUE | vzm::VzRenderer::ActorFilter::VOLUME,
								0.2f, // tolerance
								pos_picked, actor_vid))
							{
								view3D_gizmo.SetHighlighedVID(actor_vid);
								belongto = 0;
								
								vzlog("PICKING %s", vzm::GetNameByVid(actor_vid).c_str());
							}
						}
						else
						{
							mouse_control_flag = true;
							orbit_control->Start(__FC2 pos_ss);
						}
					}
					else if (mouse_control_flag &&
						((ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.f)) && glm::length2(prevMousePos - m_pos) > 0))
					{
						if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
							orbit_control->PanMove(__FC2 pos_ss);
						else
							orbit_control->Move(__FC2 pos_ss);
					}
					else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right))
					{
						mouse_control_flag = false;
					}
					else if (io.MouseWheel != 0)
					{
						//glm::fvec3 pos, view, up;
						//camera->GetWorldPose(__FC3 pos, __FC3 view, __FC3 up);
						//if (io.MouseWheel > 0)
						//	pos += 0.2f * view;
						//else
						//	pos -= 0.2f * view;
						//camera->SetWorldPose(__FC3 pos, __FC3 view, __FC3 up);
						orbit_control->Zoom(io.MouseWheel, 1.f);
					}
					prevMousePos = pos_ss;
				}

				ImGui::SetCursorPos(cur_item_pos);

				if (use_renderchain)
				{
					render_chain.push_back(ChainUnitRCam(renderer3D, camera));
				}
				else
					renderer3D->Render(scene, camera);

				uint32_t w, h;
				VzRenderer::SharedResourceTarget srt;
				if (renderer3D->GetSharedRenderTarget(g_pd3dDevice, g_pd3dSrvDescHeap, 1, srt, &w, &h)) {
					ImTextureID texId = (ImTextureID)srt.descriptorHandle;
					// https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
					//ImGui::Image(texId, ImVec2((float)w, (float)h));
					ImVec2 pos = ImGui::GetItemRectMin();
					ImVec2 size = canvas_size;
					ImVec2 pos_end = ImVec2(pos.x + size.x, pos.y + size.y);
					ImGui::GetWindowDrawList()->AddImage(texId, pos, pos_end);

					if (belongto == 0)
						view3D_gizmo.ApplyGizmo(camera->GetVID(), pos, size, ImGui::GetWindowDrawList());
				}
			}
			ImGui::End();
			
			ImGui::Begin("Curved Slicer Viewer");
			{
				static ImVec2 canvas_size_prev = ImVec2(0, 0);
				ImVec2 canvas_size = ImGui::GetContentRegionAvail();

				if (canvas_size_prev.x * canvas_size_prev.y == 0)
				{
					ImGui::SetWindowSize(ImVec2(0, 0));
					canvas_size.x = std::max(canvas_size.x, 1.f);
					canvas_size.y = std::max(canvas_size.y, 1.f);
				}

				bool resized = canvas_size_prev.x != canvas_size.x || canvas_size_prev.y != canvas_size.y;
				canvas_size_prev = canvas_size;

				if (resized)
				{
					renderer_curvedslicer->ResizeCanvas((uint)canvas_size.x, (uint)canvas_size.y, slicer_curved->GetVID());
				}
				ImVec2 win_pos = ImGui::GetWindowPos();
				ImVec2 cur_item_pos = ImGui::GetCursorPos();
				ImGui::InvisibleButton("curved slicer window", canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				ImGui::SetItemAllowOverlap();

				bool is_hovered = ImGui::IsItemHovered(); // Hovered

				if (is_hovered && !resized)
				{
					static glm::fvec2 prevMousePos(0);
					glm::fvec2 ioPos = *(glm::fvec2*)&io.MousePos;
					glm::fvec2 s_pos = *(glm::fvec2*)&cur_item_pos;
					glm::fvec2 w_pos = *(glm::fvec2*)&win_pos;
					glm::fvec2 m_pos = ioPos - s_pos - w_pos;
					glm::fvec2 pos_ss = m_pos;

					SlicerControl* slice_control = slicer_curved->GetSlicerControl();
					slice_control->Initialize(renderer_curvedslicer->GetVID(), { 0, 0, 0 });

					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
					{
						slice_control->Start(__FC2 pos_ss);
					}
					else if ((ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.f)) && glm::length2(prevMousePos - m_pos) > 0)
					{
						if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
							slice_control->PanMove(__FC2 pos_ss);
						else
							slice_control->Zoom(__FC2 pos_ss, false);
					}
					else if (io.MouseWheel != 0)
					{
						//glm::fvec3 pos, view, up;
						//camera->GetWorldPose(__FC3 pos, __FC3 view, __FC3 up);
						//if (io.MouseWheel > 0)
						//	pos += 0.2f * view;
						//else
						//	pos -= 0.2f * view;
						//camera->SetWorldPose(__FC3 pos, __FC3 view, __FC3 up);
						slice_control->Move(io.MouseWheel, 0.01f);
					}
					prevMousePos = pos_ss;
				}

				ImGui::SetCursorPos(cur_item_pos);

				if (use_renderchain)
				{
					render_chain.push_back(ChainUnitRCam(renderer_curvedslicer, slicer_curved));
				}
				else
				{
					vzm::PendingSubmitCommand(false);
					renderer_curvedslicer->Render(scene, slicer_curved);
				}

				uint32_t w, h;
				VzRenderer::SharedResourceTarget srt;
				if (renderer_curvedslicer->GetSharedRenderTarget(g_pd3dDevice, g_pd3dSrvDescHeap, 4, srt, &w, &h)) {
					ImTextureID texId = (ImTextureID)srt.descriptorHandle;
					// https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
					//ImGui::Image(texId, ImVec2((float)w, (float)h));
					ImVec2 pos = ImGui::GetItemRectMin();
					ImVec2 size = canvas_size;
					ImVec2 pos_end = ImVec2(pos.x + size.x, pos.y + size.y);
					ImGui::GetWindowDrawList()->AddImage(texId, pos, pos_end);
				}
			}
			ImGui::End();
			
			if (use_renderchain)
			{
				scene->RenderChain(render_chain);
			}

			ImGui::Begin("Controls");
			{
				if (ImGui::BeginTabBar("##gizmo_tabbar"))
				{
					VzActor* highlighted_actor = (VzActor*)GetComponent(view3D_gizmo.GetHighlighedVID());
					float matrixTranslation[3], matrixRotation[3], matrixScale[3];
					XMFLOAT4X4 obj_world;
					if (highlighted_actor)
					{
						highlighted_actor->GetLocalMatrix(__FC44 obj_world, true);
						ImGuizmo::DecomposeMatrixToComponents(&obj_world._11, matrixTranslation, matrixRotation, matrixScale);
					}

					static int old_selection = -1;
					static int input_text_flags = ImGuiInputTextFlags_ReadOnly;
					static int input_text_disable = 0;

					int flags = 0;

					//PE: ImGuiTabItemFlags_SetSelected is one frame delayed so...
					bool bChangeStatus = true;
					if (old_selection != view3D_gizmo.currentGizmoOperation)
					{
						old_selection = view3D_gizmo.currentGizmoOperation;
						bChangeStatus = false;
					}

					bool bUpdateTransform = false;

					flags = 0;
					if (view3D_gizmo.currentGizmoOperation == ImGuizmo::TRANSLATE) flags = ImGuiTabItemFlags_SetSelected;
					if (ImGui::BeginTabItem(ICON_MD_ZOOM_OUT_MAP " Pos", NULL, flags))
					{
						if (bChangeStatus) view3D_gizmo.currentGizmoOperation = ImGuizmo::TRANSLATE;
						ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 3.0f));
						vzimgui::IGTextTitle("Position");

						if (highlighted_actor != nullptr)
						{
							ImGui::PushItemWidth(-4);
							if (ImGui::InputFloat3("##Tr", matrixTranslation, "%.3f", input_text_flags))
							{
								bUpdateTransform = true;
							}
							ImGui::PopItemWidth();
						}
						ImGui::EndTabItem();
					}

					flags = 0;
					
					if (view3D_gizmo.currentGizmoOperation == ImGuizmo::ROTATE) flags = ImGuiTabItemFlags_SetSelected;
					if (ImGui::BeginTabItem(ICON_MD_3D_ROTATION " Rot", NULL, flags))
					{
						if (bChangeStatus) view3D_gizmo.currentGizmoOperation = ImGuizmo::ROTATE;
						ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 3.0f));
						vzimgui::IGTextTitle("Rotate");
						if (highlighted_actor != nullptr)
						{
							ImGui::PushItemWidth(-4);
							if (ImGui::InputFloat3("##Rt", matrixRotation, "%.3f", input_text_flags))
							{
								bUpdateTransform = true;
							}
							ImGui::PopItemWidth();
						}
						ImGui::EndTabItem();
					}

					flags = 0;
					if (view3D_gizmo.currentGizmoOperation == ImGuizmo::SCALE) flags = ImGuiTabItemFlags_SetSelected;
					if (ImGui::BeginTabItem(ICON_MD_ASPECT_RATIO " Scale", NULL, flags))
					{
						if (bChangeStatus) view3D_gizmo.currentGizmoOperation = ImGuizmo::SCALE;
						ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 3.0f));
						vzimgui::IGTextTitle("Scale");
						if (highlighted_actor != nullptr)
						{
							ImGui::PushItemWidth(-4);
							if (ImGui::InputFloat3("##Sc", matrixScale, "%.3f", input_text_flags))
							{
								bUpdateTransform = true;
							}
							ImGui::PopItemWidth();
						}
						ImGui::EndTabItem();
					}

#ifdef USEBOUNDSIZING
					flags = 0;
					if (view3D_gizmo.currentGizmoOperation == ImGuizmo::BOUNDS) flags = ImGuiTabItemFlags_SetSelected;
					if (ImGui::BeginTabItem(ICON_MD_FENCE " Bounds", NULL, flags))
					{
						if (bChangeStatus)
						{
							view3D_gizmo.boundSizing = true;
							view3D_gizmo.currentGizmoOperation = ImGuizmo::BOUNDS;
						}
						ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 3.0f));
						vzimgui::IGTextTitle("Bounds");
						if (highlighted_actor != nullptr)
						{
							ImGui::PushItemWidth(-4);
							if (ImGui::InputFloat3("##Tr", matrixTranslation, "%.3f", input_text_flags))
							{
								bUpdateTransform = true;
							}
							if (ImGui::InputFloat3("##Sc", matrixScale, "%.3f", input_text_flags))
							{
								bUpdateTransform = true;
							}
							ImGui::Checkbox("Bound Grid", &view3D_gizmo.boundSizingSnap);
							if (view3D_gizmo.boundSizingSnap)
							{
								ImGui::PushItemWidth(-4);
								ImGui::InputFloat3("##boundssnap", view3D_gizmo.boundsSnap, "%.3f", input_text_flags);
								ImGui::PopItemWidth();
							}
							ImGui::PopItemWidth();
						}
						ImGui::EndTabItem();
					}
					else
					{
						view3D_gizmo.boundSizing = false;
					}
#else
					boundSizing = false;
#endif
					flags = 0;
					if (view3D_gizmo.currentGizmoOperation == ImGuizmo::UNIVERSAL) flags = ImGuiTabItemFlags_SetSelected;
					if (ImGui::BeginTabItem("All", NULL, flags))
					{
						if (bChangeStatus) view3D_gizmo.currentGizmoOperation = ImGuizmo::UNIVERSAL;
						ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 3.0f));
						vzimgui::IGTextTitle("All");
						ImGui::PushItemWidth(-4);
						if (ImGui::InputFloat3("##Tr", matrixTranslation, "%.3f", input_text_flags))
						{
							bUpdateTransform = true;
						}
						if (ImGui::InputFloat3("##Rt", matrixRotation, "%.3f", input_text_flags))
						{
							bUpdateTransform = true;
						}
						if (ImGui::InputFloat3("##Sc", matrixScale, "%.3f", input_text_flags))
						{
							bUpdateTransform = true;
						}
						ImGui::PopItemWidth();
						ImGui::EndTabItem();
					}

					if (input_text_disable == 0 && bUpdateTransform)
					{
						ImGuizmo::RecomposeMatrixFromComponents(matrixTranslation, matrixRotation, matrixScale, &obj_world._11);

						highlighted_actor->SetMatrix(__FC44 obj_world._11, true);
						highlighted_actor->UpdateMatrix();
					}

					ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0.0f, 3.0f));
					if (!view3D_gizmo.boundSizing)
					{
						vzimgui::IGTextTitle("Grid");

						ImGui::Checkbox("Use Grid", &view3D_gizmo.useSnap);
						if (view3D_gizmo.useSnap)
						{
							ImGui::PushItemWidth(-4);
							ImGui::InputFloat3("##snap", view3D_gizmo.snap, "%.3f", input_text_flags);
							ImGui::PopItemWidth();
						}
					}
				}
				ImGui::EndTabBar();

				ImGui::Separator();
				vzimgui::IGTextTitle("----- Scene Tree -----");
				const std::vector<VID> root_children = scene->GetChildrenVIDs();
				static VID selected_vid = 0u;
				for (auto vid_root : root_children)
				{
					vzimgui::UpdateTreeNode(vid_root, selected_vid, [](const VID vid){});
				}
				ImGui::Separator();

				if (ImGui::Button("Shader Reload"))
				{
					vzm::ReloadShader();
				}

				ImGui::Checkbox("Use Render Chain", &use_renderchain);

				ImGui::SliderFloat("OTF Slider", &curOtfValue, 0.f, 250.f);
				if (curOtfValuePrev != curOtfValue)
				{
					curOtfValuePrev = curOtfValue;
					vzm::VzTexture* otf_volume = (vzm::VzTexture*)vzm::GetFirstComponentByName("volume material's OTF");
					if (otf_volume)
					{
						std::vector<uint8_t> otf_array(otfW * 4 * 1);
						for (size_t i = 0; i < otfW; i++)
						{
							uint8_t a = i < curOtfValue ? 0 :
								i < curOtfValue + curOtfBandWidth ? (uint8_t)((float)(i - curOtfValue) / curOtfBandWidth * 255.f) : 255;
							otf_array[(otfW * 4 * 0) + 4 * i + 0] = 255;
							otf_array[(otfW * 4 * 0) + 4 * i + 1] = 0;
							otf_array[(otfW * 4 * 0) + 4 * i + 2] = 0;
							otf_array[(otfW * 4 * 0) + 4 * i + 3] = a;
						}

						otf_volume->UpdateLookup(otf_array, (uint)curOtfValue, 255);
					}
				}

				ImGui::SliderFloat("Windowing Center", &curWindowCenter, 0.f, 250.f);
				ImGui::SliderFloat("Windowing Width", &curWindowBandWidth, 0.f, 250.f);
				if (curWindowCenter != curWindowCenterPrev
					|| curWindowBandWidth != curWindowBandWidthPrev)
				{
					curWindowCenterPrev = curWindowCenter;
					curWindowBandWidthPrev = curWindowBandWidth;					
					vzm::VzTexture* windowing_volume = (vzm::VzTexture*)vzm::GetFirstComponentByName("volume material's windowing");
					if (windowing_volume)
					{
						std::vector<uint8_t> otf_array(otfW * 4 * 1);
						for (size_t i = 0; i < otfW; i++)
						{
							otf_array[4 * i + 0] = 255;
							otf_array[4 * i + 1] = 255;
							otf_array[4 * i + 2] = 255;
							otf_array[4 * i + 3] = std::max(std::min((uint)(linearWindowing(i, curWindowCenter, curWindowBandWidth) * 255.f), 255u), 0u);
						}

						windowing_volume->CreateLookupTexture("windowing otf", otf_array, vzm::TextureFormat::R8G8B8A8_UNORM, otfW, 1, 1);
						windowing_volume->UpdateLookup(otf_array, curWindowCenter - curWindowBandWidth * 0.5f, curWindowCenter + curWindowBandWidth * 0.5f);
					}
				}

				static int dvr_mode = 1, dvr_mode_prev = 1;
				ImGui::RadioButton("Slicer Default", &dvr_mode, 0);
				ImGui::RadioButton("Slicer X-RAY", &dvr_mode, 1);
				if (dvr_mode != dvr_mode_prev)
				{
					dvr_mode_prev = dvr_mode;
					slicer_curved->SetDVRType((DVR_TYPE)dvr_mode);
				}

				static float cur_slicer_thickess = 0, cur_slicer_thickess_prev = 0;
				ImGui::SliderFloat("Slicer Thickness", &cur_slicer_thickess, 0.f, 10.f);
				if (cur_slicer_thickess != cur_slicer_thickess_prev)
				{
					cur_slicer_thickess_prev = cur_slicer_thickess;
					slicer_curved->SetSlicerThickness(cur_slicer_thickess);

					vzm::VzGeometry* geometry_cslicer_helper = (vzm::VzGeometry*)vzm::GetFirstComponentByName("geometry helper for curved slicer");
					slicer_curved->MakeCurvedSlicerHelperGeometry(geometry_cslicer_helper->GetVID());
				}

				ImGui::Separator();
				ImGui::Text("Rendering Options");
				static bool TAA_enabled = vz::config::GetBoolConfig("SHADER_ENGINE_SETTINGS", "TEMPORAL_AA");
				if (ImGui::Checkbox("TAA", &TAA_enabled))
				{
					vzm::ParamMap<std::string> config_options;
					config_options.SetParam("TEMPORAL_AA", TAA_enabled);
					vzm::SetConfigure(config_options);
				}

				ImGui::Separator();
				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);

				static bool profile_enabled = false;
				if (ImGui::Checkbox("Profile Enabled", &profile_enabled))
				{
					vz::profiler::SetEnabled(profile_enabled);
				}
				if (profile_enabled)
				{
					std::string performance_info, memory_info;
					vz::profiler::GetProfileInfo(performance_info, memory_info);
					ImGui::Text(scene->GetName().c_str());
					ImGui::Separator();
					ImGui::Text(performance_info.c_str());
					ImGui::Separator();
					ImGui::Text(memory_info.c_str());
				}
			}
			ImGui::End();

			ImGui::Begin("System Monitor");
			{
				vzimgui::UpdateResourceMonitor([](const VID vid) {});
			}
			ImGui::End();
		}

		// Rendering
		ImGui::PopFont();
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
		// g_pSwapChain->Present(0, 0); // Present without vsync

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
	// if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
	//	pdx12Debug->EnableDebugLayer();
#endif

	// Create device
	if ((g_pd3dDevice = CreateDeviceHelper()) == nullptr)
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
		desc.NumDescriptors = 64;
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
	if (g_pSwapChain)
	{
		g_pSwapChain->SetFullscreenState(false, nullptr);
		g_pSwapChain->Release();
		g_pSwapChain = nullptr;
	}
	if (g_hSwapChainWaitableObject != nullptr)
	{
		CloseHandle(g_hSwapChainWaitableObject);
	}
	for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
		if (g_frameContext[i].CommandAllocator)
		{
			g_frameContext[i].CommandAllocator->Release();
			g_frameContext[i].CommandAllocator = nullptr;
		}
	if (g_pd3dCommandQueue)
	{
		g_pd3dCommandQueue->Release();
		g_pd3dCommandQueue = nullptr;
	}
	if (g_pd3dCommandList)
	{
		g_pd3dCommandList->Release();
		g_pd3dCommandList = nullptr;
	}
	if (g_pd3dRtvDescHeap)
	{
		g_pd3dRtvDescHeap->Release();
		g_pd3dRtvDescHeap = nullptr;
	}
	if (g_pd3dSrvDescHeap)
	{
		g_pd3dSrvDescHeap->Release();
		g_pd3dSrvDescHeap = nullptr;
	}
	if (g_fence)
	{
		g_fence->Release();
		g_fence = nullptr;
	}
	if (g_fenceEvent)
	{
		CloseHandle(g_fenceEvent);
		g_fenceEvent = nullptr;
	}
	if (g_pd3dDevice)
	{
		g_pd3dDevice->Release();
		g_pd3dDevice = nullptr;
	}

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
		if (g_mainRenderTargetResource[i])
		{
			g_mainRenderTargetResource[i]->Release();
			g_mainRenderTargetResource[i] = nullptr;
		}
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
		switch (wParam)
		{
		case 'R':
			// vz::eventhandler::FireEvent(1, 0);
			vzm::ReloadShader();
			break;
		case '0':
			renderer->ShowDebugBuffer("NONE");
			break;
		case '1':
			renderer->ShowDebugBuffer("PRIMITIVE_ID");
			break;
		case '2':
			renderer->ShowDebugBuffer("INSTANCE_ID");
			break;
		case '3':
			renderer->ShowDebugBuffer("LINEAR_DEPTH");
			break;
		case '4':
			renderer->ShowDebugBuffer("WITHOUT_POSTPROCESSING");
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
