//#pragma warning(disable:4819)
#include "VizEngineAPIs.h" 
#include "vzEngine.h"

#include "vzGraphicsDevice_DX12.h"
#include "vzGraphicsDevice_Vulkan.h"

#include "../Helpers/ModelImporter.h"

using namespace vz::ecs;
using namespace vz::scene;

#define CANVAS_INIT_W 16u
#define CANVAS_INIT_H 16u
#define CANVAS_INIT_DPI 96.f

enum class FileType
{
	INVALID,
	OBJ,
	GLTF,
	GLB,
	VRM,
	IMAGE,
	VIDEO,
	SOUND,
};
static vz::unordered_map<std::string, FileType> filetypes = {
	{"OBJ", FileType::OBJ},
	{"GLTF", FileType::GLTF},
	{"GLB", FileType::GLB},
	{"VRM", FileType::VRM},
};

enum class VmCompType
{
	INVALID,
	ANIMATION,
	ACTOR,
	LIGHT,
};
static vz::unordered_map<std::string, VmCompType> vmcomptypes = {
	{typeid(vzm::VmActor).name(), VmCompType::ACTOR},
	{typeid(vzm::VmLight).name(), VmCompType::LIGHT},
	{typeid(vzm::VmAnimation).name(), VmCompType::ANIMATION},
};

enum class CompType
{
	INVALID,
	NameComponent_,
	LayerComponent_,
	TransformComponent_,
	HierarchyComponent_,
	MaterialComponent_,
	MeshComponent_,
	ImpostorComponent_,
	ObjectComponent_,
	LightComponent_,
	CameraComponent_,
	EnvironmentProbeComponent_,
	AnimationComponent_,
};
static vz::unordered_map<std::string, CompType> comptypes = {
	{typeid(vz::scene::NameComponent).name(), CompType::NameComponent_},
	{typeid(vz::scene::LayerComponent).name(), CompType::LayerComponent_},
	{typeid(vz::scene::TransformComponent).name(), CompType::TransformComponent_},
	{typeid(vz::scene::HierarchyComponent).name(), CompType::HierarchyComponent_},
	{typeid(vz::scene::MaterialComponent).name(), CompType::MaterialComponent_},
	{typeid(vz::scene::MeshComponent).name(), CompType::MeshComponent_},
	//{typeid(vz::scene::ObjectComponent).name(), CompType::ObjectComponent_},
	{typeid(vz::scene::LightComponent).name(), CompType::LightComponent_},
	{typeid(vz::scene::CameraComponent).name(), CompType::CameraComponent_},
	{typeid(vz::scene::EnvironmentProbeComponent).name(), CompType::EnvironmentProbeComponent_},
	{typeid(vz::scene::AnimationComponent).name(), CompType::AnimationComponent_},
};




static bool g_is_display = true;
auto fail_ret = [](const std::string& err_str, const bool _warn = false)
	{
		if (g_is_display)
		{
			vz::backlog::post(err_str, _warn ? vz::backlog::LogLevel::Warning : vz::backlog::LogLevel::Error);
		}
		return false;
	};


namespace vzm
{
	void TransformPoint(const float posSrc[3], const float mat[16], const bool rowMajor, float posDst[3])
	{
		XMVECTOR p = XMLoadFloat3((XMFLOAT3*)posSrc);
		XMMATRIX m(mat);
		if (!rowMajor) {
			m = XMMatrixTranspose(m);
		}
		p = XMVector3TransformCoord(p, m);
		XMStoreFloat3((XMFLOAT3*)posDst, p);
	}
	void TransformVector(const float vecSrc[3], const float mat[16], const bool rowMajor, float vecDst[3])
	{
		XMVECTOR v = XMLoadFloat3((XMFLOAT3*)vecSrc);
		XMMATRIX m(mat);
		if (!rowMajor) {
			m = XMMatrixTranspose(m);
		}
		v = XMVector3TransformNormal(v, m);
		XMStoreFloat3((XMFLOAT3*)vecDst, v);
	}
	void ComputeBoxTransformMatrix(const float cubeScale[3], const float posCenter[3],
		const float yAxis[3], const float zAxis[3], const bool rowMajor, float mat[16], float matInv[16])
	{
		XMVECTOR vec_scale = XMLoadFloat3((XMFLOAT3*)cubeScale);
		XMVECTOR pos_eye = XMLoadFloat3((XMFLOAT3*)posCenter);
		XMVECTOR vec_up = XMLoadFloat3((XMFLOAT3*)yAxis);
		XMVECTOR vec_view = -XMLoadFloat3((XMFLOAT3*)zAxis);
		XMMATRIX ws2cs = VZMatrixLookTo(pos_eye, vec_view, vec_up);
		vec_scale = XMVectorReciprocal(vec_scale);
		XMMATRIX scale = XMMatrixScaling(XMVectorGetX(vec_scale), XMVectorGetY(vec_scale), XMVectorGetZ(vec_scale));
		//glm::fmat4x4 translate = glm::translate(glm::fvec3(0.5f));

		XMMATRIX ws2cs_unit = ws2cs * scale; // row major
		*(XMMATRIX*)mat = rowMajor ? ws2cs_unit : XMMatrixTranspose(ws2cs_unit); // note that our renderer uses row-major
		*(XMMATRIX*)matInv = XMMatrixInverse(NULL, ws2cs_unit);
	}

	using namespace vz;

	class VzmRenderer : public vz::RenderPath3D
	{
	private:
		uint32_t prev_width = 0;
		uint32_t prev_height = 0;
		float prev_dpi = 96;
		bool prev_colorspace_conversion_required = false;

		std::unique_ptr<VmCamera> vmCam;
		TimeStamp timeStamp_vmUpdate;

		void TryResizeRenderTargets()
		{
			vz::graphics::GraphicsDevice* graphicsDevice = vz::graphics::GetDevice();
			if (graphicsDevice == nullptr)
				return;

			if (swapChain.IsValid())
			{
				colorspace = graphicsDevice->GetSwapChainColorSpace(&swapChain);
			}
			colorspace_conversion_required = colorspace == vz::graphics::ColorSpace::HDR10_ST2084;

			bool requireUpdateRenderTarget = prev_width != width || prev_height != height || prev_dpi != dpi
				|| prev_colorspace_conversion_required != colorspace_conversion_required;
			if (!requireUpdateRenderTarget)
				return;

			init(width, height, dpi);
			swapChain = {};
			renderResult = {};
			renderInterResult = {};

			auto CreateRenderTarget = [&](vz::graphics::Texture& renderTexture, const bool isInterResult)
				{
					if (!renderTexture.IsValid())
					{
						vz::graphics::TextureDesc desc;
						desc.width = width;
						desc.height = height;
						desc.bind_flags = vz::graphics::BindFlag::RENDER_TARGET | vz::graphics::BindFlag::SHADER_RESOURCE;
						if (!isInterResult)
						{
							// we assume the main GUI and engine use the same GPU device
							// vz::graphics::ResourceMiscFlag::SHARED_ACROSS_ADAPTER;
							desc.misc_flags = vz::graphics::ResourceMiscFlag::SHARED;
							desc.format = vz::graphics::Format::R10G10B10A2_UNORM;
						}
						else
						{
							desc.format = vz::graphics::Format::R11G11B10_FLOAT;
						}
						bool success = graphicsDevice->CreateTexture(&desc, nullptr, &renderTexture);
						assert(success);

						graphicsDevice->SetName(&renderTexture, (isInterResult ? "VzmRenderer::renderInterResult_" : "VzmRenderer::renderResult_") + camEntity);
					}
				};
			if (colorspace_conversion_required)
			{
				CreateRenderTarget(renderInterResult, true);
			}
			if (swapChain.IsValid())
			{
				// dojo to do ... create swapchain...
				// a window handler required 
			}
			else
			{
				CreateRenderTarget(renderResult, false);
			}

			Start(); // call ResizeBuffers();
			prev_width = width;
			prev_height = height;
			prev_dpi = dpi;
			prev_colorspace_conversion_required = colorspace_conversion_required;
		}

	public:

		float deltaTime = 0;
		float deltaTimeAccumulator = 0;
		float targetFrameRate = 60;
		bool frameskip = true;
		bool framerate_lock = false;
		vz::Timer timer;
		int fps_avg_counter = 0;

		vz::FadeManager fadeManager;

		// render target 을 compose... 
		Entity camEntity = INVALID_ENTITY; // for searching 
		bool colorspace_conversion_required = false;
		vz::graphics::ColorSpace colorspace = vz::graphics::ColorSpace::SRGB;

		// note swapChain and renderResult are exclusive
		vz::graphics::SwapChain swapChain;
		vz::graphics::Texture renderResult;
		// renderInterResult is valid only when swapchain's color space is ColorSpace::HDR10_ST2084
		vz::graphics::Texture renderInterResult;

		bool gridHelper = false;

		struct InfoDisplayer
		{
			// activate the whole display
			bool active = false;
			// display engine version number
			bool watermark = true;
			// display framerate
			bool fpsinfo = false;
			// display graphics device name
			bool device_name = false;
			// display resolution info
			bool resolution = false;
			// window's size in logical (DPI scaled) units
			bool logical_size = false;
			// HDR status and color space
			bool colorspace = false;
			// display number of heap allocations per frame
			bool heap_allocation_counter = false;
			// display the active graphics pipeline count
			bool pipeline_count = false;
			// display video memory usage and budget
			bool vram_usage = false;
			// text size
			int size = 16;
			// display default color grading helper texture in top left corner of the screen
			bool colorgrading_helper = false;
		};
		// display all-time engine information text
		InfoDisplayer infoDisplay;
		std::string infodisplay_str;
		float deltatimes[20] = {};

		// kind of initializer
		void Load() override
		{

			this->ClearSprites();
			this->ClearFonts();

			gridHelper = false;
			infoDisplay.active = true;
			infoDisplay.watermark = true;
			infoDisplay.fpsinfo = true;
			infoDisplay.resolution = true;
			infoDisplay.colorspace = true;
			infoDisplay.device_name = true;
			infoDisplay.vram_usage = true;
			infoDisplay.heap_allocation_counter = true;

			// remove...
			vz::renderer::SetToDrawGridHelper(true);
			vz::renderer::SetVXGIReflectionsEnabled(false);
			vz::renderer::SetTemporalAAEnabled(true);
			setSSREnabled(false);
			setFXAAEnabled(false);
			setReflectionsEnabled(true);
			setRaytracedReflectionsEnabled(true);
			setFSR2Enabled(false);
			setEyeAdaptionEnabled(true);
			setEyeAdaptionKey(0.1f);
			setEyeAdaptionRate(0.5f);

			vz::font::UpdateAtlas(GetDPIScaling());

			RenderPath3D::Load();

			assert(width > 0 && height > 0);
			{
				const float fadeSeconds = 0.f;
				vz::Color fadeColor = vz::Color(0, 0, 0, 255);
				// Fade manager will activate on fadeout
				fadeManager.Clear();
				fadeManager.Start(fadeSeconds, fadeColor, [this]() {
					Start();
					});

				fadeManager.Update(0); // If user calls ActivatePath without fadeout, it will be instant
			}
		}

		void Update(float dt) override
		{
			RenderPath3D::Update(dt);	// calls RenderPath2D::Update(dt);
		}

		void PostUpdate() override
		{
			RenderPath2D::PostUpdate();
			RenderPath3D::PostUpdate();
		}

		void FixedUpdate() override
		{
			RenderPath2D::FixedUpdate();
		}

		void Compose(vz::graphics::CommandList cmd)
		{
			using namespace vz::graphics;

			vz::graphics::GraphicsDevice* graphicsDevice = GetDevice();
			if (!graphicsDevice)
				return;

			auto range = vz::profiler::BeginRangeCPU("Compose");

			vz::RenderPath3D::Compose(cmd);

			if (fadeManager.IsActive())
			{
				// display fade rect
				vz::image::Params fx;
				fx.enableFullScreen();
				fx.color = fadeManager.color;
				fx.opacity = fadeManager.opacity;
				vz::image::Draw(nullptr, fx, cmd);
			}

			// Draw the information display
			if (infoDisplay.active)
			{
				infodisplay_str.clear();
				if (infoDisplay.watermark)
				{
					infodisplay_str += "Wicked Engine ";
					infodisplay_str += vz::version::GetVersionString();
					infodisplay_str += " ";

#if defined(_ARM)
					infodisplay_str += "[ARM]";
#elif defined(_WIN64)
					infodisplay_str += "[64-bit]";
#elif defined(_WIN32)
					infodisplay_str += "[32-bit]";
#endif // _ARM

#ifdef PLATFORM_UWP
					infodisplay_str += "[UWP]";
#endif // PLATFORM_UWP

#ifdef VZMENGINE_BUILD_DX12
					if (dynamic_cast<GraphicsDevice_DX12*>(graphicsDevice))
					{
						infodisplay_str += "[DX12]";
					}
#endif // VZMENGINE_BUILD_DX12
#ifdef VZMENGINE_BUILD_VULKAN
					if (dynamic_cast<GraphicsDevice_Vulkan*>(graphicsDevice))
					{
						infodisplay_str += "[Vulkan]";
					}
#endif // VZMENGINE_BUILD_VULKAN
#ifdef PLATFORM_PS5
					if (dynamic_cast<GraphicsDevice_PS5*>(graphicsDevice.get()))
					{
						infodisplay_str += "[PS5]";
					}
#endif // PLATFORM_PS5

#ifdef _DEBUG
					infodisplay_str += "[DEBUG]";
#endif // _DEBUG
					if (graphicsDevice->IsDebugDevice())
					{
						infodisplay_str += "[debugdevice]";
					}
					infodisplay_str += "\n";
				}
				if (infoDisplay.device_name)
				{
					infodisplay_str += "Device: ";
					infodisplay_str += graphicsDevice->GetAdapterName();
					infodisplay_str += "\n";
				}
				if (infoDisplay.resolution)
				{
					infodisplay_str += "Resolution: ";
					infodisplay_str += std::to_string(GetPhysicalWidth());
					infodisplay_str += " x ";
					infodisplay_str += std::to_string(GetPhysicalHeight());
					infodisplay_str += " (";
					infodisplay_str += std::to_string(int(GetDPI()));
					infodisplay_str += " dpi)\n";
				}
				if (infoDisplay.logical_size)
				{
					infodisplay_str += "Logical Size: ";
					infodisplay_str += std::to_string(int(GetLogicalWidth()));
					infodisplay_str += " x ";
					infodisplay_str += std::to_string(int(GetLogicalHeight()));
					infodisplay_str += "\n";
				}
				if (infoDisplay.colorspace)
				{
					infodisplay_str += "Color Space: ";
					ColorSpace colorSpace = colorspace; // graphicsDevice->GetSwapChainColorSpace(&swapChain);
					switch (colorSpace)
					{
					default:
					case vz::graphics::ColorSpace::SRGB:
						infodisplay_str += "sRGB";
						break;
					case vz::graphics::ColorSpace::HDR10_ST2084:
						infodisplay_str += "ST.2084 (HDR10)";
						break;
					case vz::graphics::ColorSpace::HDR_LINEAR:
						infodisplay_str += "Linear (HDR)";
						break;
					}
					infodisplay_str += "\n";
				}
				if (infoDisplay.fpsinfo)
				{
					deltatimes[fps_avg_counter++ % arraysize(deltatimes)] = deltaTime;
					float displaydeltatime = deltaTime;
					if (fps_avg_counter > arraysize(deltatimes))
					{
						float avg_time = 0;
						for (int i = 0; i < arraysize(deltatimes); ++i)
						{
							avg_time += deltatimes[i];
						}
						displaydeltatime = avg_time / arraysize(deltatimes);
					}

					infodisplay_str += std::to_string(int(std::round(1.0f / displaydeltatime))) + " FPS\n";
				}
				if (infoDisplay.heap_allocation_counter)
				{
					infodisplay_str += "Heap allocations per frame: ";
#ifdef VZM_ENGINE_HEAP_ALLOCATION_COUNTER
					infodisplay_str += std::to_string(number_of_heap_allocations.load());
					infodisplay_str += " (";
					infodisplay_str += std::to_string(size_of_heap_allocations.load());
					infodisplay_str += " bytes)\n";
					number_of_heap_allocations.store(0);
					size_of_heap_allocations.store(0);
#else
					infodisplay_str += "[disabled]\n";
#endif // VZM_ENGINE_HEAP_ALLOCATION_COUNTER
				}
				if (infoDisplay.pipeline_count)
				{
					infodisplay_str += "Graphics pipelines active: ";
					infodisplay_str += std::to_string(graphicsDevice->GetActivePipelineCount());
					infodisplay_str += "\n";
				}

				vz::font::Params params = vz::font::Params(
					4,
					4,
					infoDisplay.size,
					vz::font::WIFALIGN_LEFT,
					vz::font::WIFALIGN_TOP,
					vz::Color::White(),
					vz::Color::Shadow()
				);
				params.shadow_softness = 0.4f;

				// Explanation: this compose pass is in LINEAR space if display output is linear or HDR10
				//	If HDR10, the HDR10 output mapping will be performed on whole image later when drawing to swapchain
				if (colorspace != ColorSpace::SRGB)
				{
					params.enableLinearOutputMapping(9);
				}

				params.cursor = vz::font::Draw(infodisplay_str, params, cmd);

				// VRAM:
				{
					GraphicsDevice::MemoryUsage vram = graphicsDevice->GetMemoryUsage();
					bool warn = false;
					if (vram.usage > vram.budget)
					{
						params.color = vz::Color::Error();
						warn = true;
					}
					else if (float(vram.usage) / float(vram.budget) > 0.9f)
					{
						params.color = vz::Color::Warning();
						warn = true;
					}
					if (infoDisplay.vram_usage || warn)
					{
						params.cursor = vz::font::Draw("VRAM usage: " + std::to_string(vram.usage / 1024 / 1024) + "MB / " + std::to_string(vram.budget / 1024 / 1024) + "MB\n", params, cmd);
						params.color = vz::Color::White();
					}
				}

				// Write warnings below:
				params.color = vz::Color::Warning();
#ifdef _DEBUG
				params.cursor = vz::font::Draw("Warning: This is a [DEBUG] build, performance will be slow!\n", params, cmd);
#endif
				if (graphicsDevice->IsDebugDevice())
				{
					params.cursor = vz::font::Draw("Warning: Graphics is in [debugdevice] mode, performance will be slow!\n", params, cmd);
				}

				// Write errors below:
				params.color = vz::Color::Error();
				if (vz::renderer::GetShaderMissingCount() > 0)
				{
					params.cursor = vz::font::Draw(std::to_string(vz::renderer::GetShaderMissingCount()) + " shaders missing! Check the backlog for more information!\n", params, cmd);
				}
				if (vz::renderer::GetShaderErrorCount() > 0)
				{
					params.cursor = vz::font::Draw(std::to_string(vz::renderer::GetShaderErrorCount()) + " shader compilation errors! Check the backlog for more information!\n", params, cmd);
				}


				if (infoDisplay.colorgrading_helper)
				{
					vz::image::Draw(vz::texturehelper::getColorGradeDefault(), vz::image::Params(0, 0, 256.0f / GetDPIScaling(), 16.0f / GetDPIScaling()), cmd);
				}
			}

			vz::profiler::DrawData(*this, 4, 10, cmd, colorspace);

			vz::backlog::Draw(*this, cmd, colorspace);

			vz::profiler::EndRange(range); // Compose
		}

		void RenderFinalize()
		{
			using namespace vz::graphics;

			vz::graphics::GraphicsDevice* graphicsDevice = GetDevice();
			if (!graphicsDevice)
				return;

			// Begin final compositing:
			CommandList cmd = graphicsDevice->BeginCommandList();
			vz::image::SetCanvas(*this);
			vz::font::SetCanvas(*this);

			Viewport viewport;
			viewport.width = (float)width;
			viewport.height = (float)height;
			graphicsDevice->BindViewports(1, &viewport, cmd);

			if (colorspace_conversion_required)
			{
				RenderPassImage rp[] = {
					RenderPassImage::RenderTarget(&renderInterResult, RenderPassImage::LoadOp::CLEAR),
				};
				graphicsDevice->RenderPassBegin(rp, arraysize(rp), cmd);
			}
			else
			{
				// If swapchain is SRGB or Linear HDR, it can be used for blending
				//	- If it is SRGB, the render path will ensure tonemapping to SDR
				//	- If it is Linear HDR, we can blend trivially in linear space
				renderInterResult = {};
				if (swapChain.IsValid())
				{
					graphicsDevice->RenderPassBegin(&swapChain, cmd);
				}
				else
				{
					RenderPassImage rp[] = {
						RenderPassImage::RenderTarget(&renderResult, RenderPassImage::LoadOp::LOAD),
					};
					graphicsDevice->RenderPassBegin(rp, arraysize(rp), cmd);
				}
			}

			Compose(cmd);
			graphicsDevice->RenderPassEnd(cmd);

			if (colorspace_conversion_required)
			{
				// In HDR10, we perform a final mapping from linear to HDR10, into the swapchain
				if (swapChain.IsValid())
				{
					graphicsDevice->RenderPassBegin(&swapChain, cmd);
				}
				else
				{
					RenderPassImage rp[] = {
						RenderPassImage::RenderTarget(&renderResult, RenderPassImage::LoadOp::LOAD),
					};
					graphicsDevice->RenderPassBegin(rp, arraysize(rp), cmd);
				}
				vz::image::Params fx;
				fx.enableFullScreen();
				fx.enableHDR10OutputMapping();
				vz::image::Draw(&renderInterResult, fx, cmd);
				graphicsDevice->RenderPassEnd(cmd);
			}

			vz::profiler::EndFrame(cmd);
			graphicsDevice->SubmitCommandLists();
		}

		void WaitRender()
		{
			vz::graphics::GraphicsDevice* graphicsDevice = vz::graphics::GetDevice();
			if (!graphicsDevice)
				return;

			vz::graphics::CommandList cmd = graphicsDevice->BeginCommandList();
			if (swapChain.IsValid())
			{
				graphicsDevice->RenderPassBegin(&swapChain, cmd);
			}
			else
			{
				vz::graphics::RenderPassImage rt[] = {
					vz::graphics::RenderPassImage::RenderTarget(&renderResult, vz::graphics::RenderPassImage::LoadOp::CLEAR)
				};
				graphicsDevice->RenderPassBegin(rt, 1, cmd);
			}

			vz::graphics::Viewport viewport;
			viewport.width = (float)width;
			viewport.height = (float)height;
			graphicsDevice->BindViewports(1, &viewport, cmd);
			if (vz::initializer::IsInitializeFinished(vz::initializer::INITIALIZED_SYSTEM_FONT))
			{
				vz::backlog::DrawOutputText(*this, cmd);
			}
			graphicsDevice->RenderPassEnd(cmd);
			graphicsDevice->SubmitCommandLists();
		}

		bool UpdateVmCamera(const VmCamera* _vmCam = nullptr)
		{
			// note Scene::Merge... rearrange the components
			camera = scene->cameras.GetComponent(camEntity);

			if (_vmCam != nullptr)
			{
				vmCam = std::make_unique<VmCamera>(*_vmCam);
				camEntity = vmCam->componentVID;
			}
			std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(timeStamp_vmUpdate - vmCam->timeStamp);
			if (time_span.count() > 0)
			{
				return true;
			}
			TryResizeRenderTargets();

			// ???? DOJO TO DO... SetPose 에서 해 보기... flickering 동작 확인
			TransformComponent* transform = scene->transforms.GetComponent(camEntity);
			if (transform)
			{
				transform->UpdateTransform();
			}
			else
			{
				camera->UpdateCamera();
			}
			timeStamp_vmUpdate = std::chrono::high_resolution_clock::now();

			return true;
		}

		VmCamera* GetVmCamera()
		{
			return vmCam.get();
		}
	};

	struct VzmScene : Scene
	{
		VID sceneId = INVALID_VID;
		std::string name;
	};

	class SceneManager
	{
	private:
		std::unique_ptr<vz::graphics::GraphicsDevice> graphicsDevice;

		// archive
		std::map<VID, VzmScene> scenes;						// <SceneEntity, Scene>
		// one camera component to one renderer
		std::map<VID, VzmRenderer> renderers;				// <CamEntity, VzmRenderer> (including camera and scene)
		std::map<VID, std::unique_ptr<VmCompBase>> vmComponents;

		std::map<std::string, VID> nameMap;				// not allowed redundant name

	public:

		void Initialize(::vzm::ParamMap<std::string>& argument)
		{
			// device creation
			// User can also create a graphics device if custom logic is desired, but they must do before this function!
			vz::platform::window_type window = argument.GetParam("window", vz::platform::window_type(nullptr));
			if (graphicsDevice == nullptr)
			{
				using namespace vz::graphics;

				ValidationMode validationMode = ValidationMode::Disabled;
				if (argument.GetParam("debugdevice", false))
				{
					validationMode = ValidationMode::Enabled;
				}
				if (argument.GetParam("gpuvalidation", false))
				{
					validationMode = ValidationMode::GPU;
				}
				if (argument.GetParam("gpu_verbose", false))
				{
					validationMode = ValidationMode::Verbose;
				}

				GPUPreference preference = GPUPreference::Discrete;
				if (argument.GetParam("igpu", false))
				{
					preference = GPUPreference::Integrated;
				}

				bool use_dx12 = vz::arguments::HasArgument("dx12");
				bool use_vulkan = vz::arguments::HasArgument("vulkan");

#ifndef VZMENGINE_BUILD_DX12
				if (use_dx12) {
					vz::helper::messageBox("The engine was built without DX12 support!", "Error");
					use_dx12 = false;
				}
#endif // VZMENGINE_BUILD_DX12
#ifndef VZMENGINE_BUILD_VULKAN
				if (use_vulkan) {
					vz::helper::messageBox("The engine was built without Vulkan support!", "Error");
					use_vulkan = false;
				}
#endif // VZMENGINE_BUILD_VULKAN

				if (!use_dx12 && !use_vulkan)
				{
#if defined(VZMENGINE_BUILD_DX12)
					use_dx12 = true;
#elif defined(VZMENGINE_BUILD_VULKAN)
					use_vulkan = true;
#else
					vz::backlog::post("No rendering backend is enabled! Please enable at least one so we can use it as default", vz::backlog::LogLevel::Error);
					assert(false);
#endif
				}
				assert(use_dx12 || use_vulkan);

				if (use_vulkan)
				{
#ifdef VZMENGINE_BUILD_VULKAN
					vz::renderer::SetShaderPath(vz::renderer::GetShaderPath() + "spirv/");
					graphicsDevice = std::make_unique<GraphicsDevice_Vulkan>(window, validationMode, preference);
#endif
				}
				else if (use_dx12)
				{
#ifdef VZMENGINE_BUILD_DX12
					vz::renderer::SetShaderPath(vz::renderer::GetShaderPath() + "hlsl6/");
					graphicsDevice = std::make_unique<GraphicsDevice_DX12>(validationMode, preference);
#endif
				}
			}
			vz::graphics::GetDevice() = graphicsDevice.get();

			vz::initializer::InitializeComponentsAsync();
			//vz::initializer::InitializeComponentsImmediate();

			// Reset all state that tests might have modified:
			vz::eventhandler::SetVSync(true);
			//vz::renderer::SetToDrawGridHelper(false);
			//vz::renderer::SetTemporalAAEnabled(false);
			//vz::renderer::ClearWorld(vz::scene::GetScene());
		}

		// Runtime can create a new entity with this
		inline Entity CreateSceneEntity(const std::string& name)
		{
			auto sid = nameMap.find(name);
			if (sid != nameMap.end())
			{
				vz::backlog::post(name + " is already registered!", backlog::LogLevel::Error);
				return INVALID_ENTITY;
			}

			Entity ett = CreateEntity();

			if (ett != INVALID_ENTITY) {
				VzmScene& scene = scenes[ett];
				vz::renderer::ClearWorld(scene);
				scene.weather = WeatherComponent();
				scene.weather.ambient = XMFLOAT3(0.9f, 0.9f, 0.9f);
				vz::Color default_sky_zenith = vz::Color(30, 40, 60, 200);
				scene.weather.zenith = default_sky_zenith;
				vz::Color default_sky_horizon = vz::Color(10, 10, 20, 220); // darker elements will lerp towards this
				scene.weather.horizon = default_sky_horizon;
				scene.weather.fogStart = std::numeric_limits<float>::max();
				scene.weather.fogDensity = 0;
				scene.name = name;
				scene.sceneId = ett;
				nameMap[name] = ett;
			}

			return ett;
		}
		inline VzmRenderer* CreateRenderer(const VID camEntity)
		{
			auto it = renderers.find(camEntity);
			assert(it == renderers.end());

			VzmRenderer* renderer = &renderers[camEntity];
			renderer->camEntity = camEntity;

			for (auto it = scenes.begin(); it != scenes.end(); it++)
			{
				VzmScene* scene = &it->second;
				CameraComponent* camera = scene->cameras.GetComponent(camEntity);
				if (camera)
				{
					renderer->scene = scene;
					renderer->camera = camera;

					nameMap[scene->names.GetComponent(camEntity)->name] = camEntity;
					return renderer;
				}
			}
			renderers.erase(camEntity);
			return nullptr;
		}

		inline VID GetVidByName(const std::string& name)
		{
			for (auto it = scenes.begin(); it != scenes.end(); it++)
			{
				Entity ett = it->second.Entity_FindByName(name);
				if (ett != INVALID_ENTITY)
				{
					return ett;
				}
				if (it->second.name == name)
				{
					return it->second.sceneId;
				}
			}
			return INVALID_VID;
		}
		inline std::string GetNameByVid(const VID vid)
		{
			for (auto it = scenes.begin(); it != scenes.end(); it++)
			{
				NameComponent* nameComp = it->second.names.GetComponent(vid);
				if (nameComp)
				{
					return nameComp->name;
				}
			}
			auto it = scenes.find(vid);
			if (it != scenes.end())
			{
				return it->second.name;
			}
			return "";
		}
		inline VzmScene* GetScene(const VID sid)
		{
			auto it = scenes.find(sid);
			if (it == scenes.end())
			{
				return nullptr;
			}
			return &it->second;
		}
		inline VzmScene* GetSceneByName(const std::string& name)
		{
			return GetScene(GetVidByName(name));
		}
		inline std::map<VID, VzmScene>& GetScenes()
		{
			return scenes;
		}
		inline VzmRenderer* GetRenderer(const VID camEntity)
		{
			auto it = renderers.find(camEntity);
			if (it == renderers.end())
			{
				return nullptr;
			}
			return &it->second;
		}
		inline VzmRenderer* GetRendererByName(const std::string& name)
		{
			return GetRenderer(GetVidByName(name));
		}

		template <typename VMCOMP>
		inline VMCOMP* CreateVmComp(const VID vid)
		{
			auto it = vmComponents.find(vid);
			assert(it == vmComponents.end());

			std::string typeName = typeid(VMCOMP).name();
			VmCompType compType = vmcomptypes[typeName];
			if (compType == VmCompType::INVALID)
			{
				return nullptr;
			}

			for (auto sit = scenes.begin(); sit != scenes.end(); sit++)
			{
				VzmScene* scene = &sit->second;
				vz::unordered_set<Entity> entities;
				scene->FindAllEntities(entities);
				auto it = entities.find(vid);
				if (it != entities.end())
				{
					VMCOMP vmComp;
					vmComp.componentVID = vid;
					vmComp.COMP_TYPE = typeName;
					vmComponents.insert(std::make_pair(vid, std::make_unique<VMCOMP>(vmComp)));
					nameMap[scene->names.GetComponent(vid)->name] = vid;
					return (VMCOMP*)vmComponents[vid].get();
				}
			}
			return nullptr;
		}

		template <typename VMCOMP>
		inline VMCOMP* GetVmComp(const VID vid)
		{
			auto it = vmComponents.find(vid);
			if (it == vmComponents.end())
			{
				return nullptr;
			}
			return (VMCOMP*)it->second.get();
		}
		template <typename COMP>
		inline COMP* GetEngineComp(const VID vid)
		{
			std::string typeName = typeid(COMP).name();
			CompType compType = comptypes[typeName];
			COMP* comp = nullptr;
			for (auto it = scenes.begin(); it != scenes.end(); it++)
			{
				VzmScene* scene = &it->second;
				switch (compType)
				{
				case CompType::CameraComponent_:
					comp = (COMP*)scene->cameras.GetComponent(vid); break;
				case CompType::ObjectComponent_:
					comp = (COMP*)scene->objects.GetComponent(vid); break;
				case CompType::TransformComponent_:
					comp = (COMP*)scene->transforms.GetComponent(vid); break;
				case CompType::HierarchyComponent_:
					comp = (COMP*)scene->hierarchy.GetComponent(vid); break;
				case CompType::NameComponent_:
					comp = (COMP*)scene->names.GetComponent(vid); break;
				case CompType::LightComponent_:
					comp = (COMP*)scene->lights.GetComponent(vid); break;
				case CompType::AnimationComponent_:
					comp = (COMP*)scene->animations.GetComponent(vid); break;
				default: assert(0 && "Not allowed GetComponent");  return nullptr;
				}
				if (comp) break;
			}

			return comp;
		}

		inline void RemoveEntity(Entity entity)
		{
			auto removeName = [&](Entity entity)
				{
					for (auto it2 : nameMap)
					{
						if (entity == it2.second)
						{
							nameMap.erase(it2.first);
							return;
						}
					}
				};

			VzmScene* scene = GetScene(entity);
			if (scene)
			{
				vz::unordered_set<Entity> entities;
				scene->FindAllEntities(entities);

				for (auto it = entities.begin(); it != entities.end(); it++)
				{
					renderers.erase(*it);
					vmComponents.erase(*it);
					removeName(*it);
				}
				scenes.erase(entity);
				removeName(entity);
			};
		}
	};

	SceneManager sceneManager;
}

namespace vzm
{
	// VmCamera
	void VmCamera::SetPose(const float pos[3], const float view[3], const float up[3])
	{
		CameraComponent* camComponent = sceneManager.GetEngineComp<CameraComponent>(componentVID);
		if (!camComponent || !renderer) return;

		// up vector correction
		XMVECTOR _eye = XMLoadFloat3((XMFLOAT3*)pos);
		XMVECTOR _view = XMVector3Normalize(XMLoadFloat3((XMFLOAT3*)view));
		XMVECTOR _up = XMLoadFloat3((XMFLOAT3*)up);
		XMVECTOR _right = XMVector3Cross(_view, _up);
		_up = XMVector3Normalize(XMVector3Cross(_right, _view));


		TransformComponent* transform = ((VzmRenderer*)renderer)->scene->transforms.GetComponent(componentVID);
		if (transform)
		{
			XMMATRIX _V = VZMatrixLookTo(_eye, _view, _up);
			XMMATRIX _InvV = XMMatrixInverse(nullptr, _V);
			transform->ClearTransform();
			transform->MatrixTransform(_InvV);

			//transform->UpdateTransform();
			//camComponent->TransformCamera(*transform);
			//camComponent->UpdateCamera();
		}
		else
		{
			// Note At is the view direction
			camComponent->Eye = *(XMFLOAT3*)pos;
			XMStoreFloat3(&camComponent->At, _view);
			XMStoreFloat3(&camComponent->Up, _up);
			//camComponent->UpdateCamera();
		}

		timeStamp = std::chrono::high_resolution_clock::now();
	}
	void VmCamera::SetPerspectiveProjection(const float zNearP, const float zFarP, const float fovY, const float aspectRatio)
	{
		CameraComponent* camComponent = sceneManager.GetEngineComp<CameraComponent>(componentVID);
		if (!camComponent) return;
		// aspectRatio is W / H
		camComponent->CreatePerspective(aspectRatio, 1.f, zNearP, zFarP, fovY);
		timeStamp = std::chrono::high_resolution_clock::now();
	}
	void VmCamera::SetCanvasSize(const float w, const float h, const float dpi)
	{
		CameraComponent* camComponent = sceneManager.GetEngineComp<CameraComponent>(componentVID);
		if (!camComponent || !renderer) return;
		camComponent->CreatePerspective(w, h, camComponent->zNearP, camComponent->zFarP, camComponent->fov);
		((VzmRenderer*)renderer)->init(std::max((uint32_t)w, 16u), std::max((uint32_t)h, 16u), dpi);
		timeStamp = std::chrono::high_resolution_clock::now();
	}
	void VmCamera::GetPose(float pos[3], float view[3], float up[3])
	{
		CameraComponent* camComponent = sceneManager.GetEngineComp<CameraComponent>(componentVID);
		if (!camComponent) return;
		if (pos) *(XMFLOAT3*)pos = camComponent->Eye;
		if (view) *(XMFLOAT3*)view = camComponent->At;
		if (up) *(XMFLOAT3*)up = camComponent->Up;
	}
	void VmCamera::GetPerspectiveProjection(float* zNearP, float* zFarP, float* fovY, float* aspectRatio)
	{
		CameraComponent* camComponent = sceneManager.GetEngineComp<CameraComponent>(componentVID);
		if (!camComponent) return;
		if (zNearP) *zNearP = camComponent->zNearP;
		if (zFarP) *zFarP = camComponent->zFarP;
		if (fovY) *fovY = camComponent->fov;
		if (aspectRatio) *aspectRatio = camComponent->width / camComponent->height;
	}
	void VmCamera::GetCanvasSize(float* w, float* h, float* dpi)
	{
		if (!renderer) return;
		if (w) *w = (float)((VzmRenderer*)renderer)->width;
		if (h) *h = (float)((VzmRenderer*)renderer)->height;
		if (dpi) *dpi = ((VzmRenderer*)renderer)->dpi;
	}

	// VmActor
	// VmLight

	// VmAnimation
	void VmAnimation::Play()
	{
		AnimationComponent* aniComponent = sceneManager.GetEngineComp<AnimationComponent>(componentVID);
		if (!aniComponent) return;
		aniComponent->Play();
	}
	void VmAnimation::Pause()
	{
		AnimationComponent* aniComponent = sceneManager.GetEngineComp<AnimationComponent>(componentVID);
		if (!aniComponent) return;
		aniComponent->Pause();
	}
	void VmAnimation::Stop()
	{
		AnimationComponent* aniComponent = sceneManager.GetEngineComp<AnimationComponent>(componentVID);
		if (!aniComponent) return;
		aniComponent->Stop();
	}
	void VmAnimation::SetLooped(const bool value)
	{
		AnimationComponent* aniComponent = sceneManager.GetEngineComp<AnimationComponent>(componentVID);
		if (!aniComponent) return;
		aniComponent->SetLooped(value);
	}
}

namespace vzm
{
	VZRESULT InitEngineLib(const std::string& coreName, const std::string& logFileName)
	{
		static bool initialized = false;
		if (initialized)
		{
			return VZ_OK;
		}

		auto ext_video = vz::resourcemanager::GetSupportedVideoExtensions();
		for (auto& x : ext_video)
		{
			filetypes[x] = FileType::VIDEO;
		}
		auto ext_sound = vz::resourcemanager::GetSupportedSoundExtensions();
		for (auto& x : ext_sound)
		{
			filetypes[x] = FileType::SOUND;
		}
		auto ext_image = vz::resourcemanager::GetSupportedImageExtensions();
		for (auto& x : ext_image)
		{
			filetypes[x] = FileType::IMAGE;
		}

		ParamMap<std::string> arguments;
		sceneManager.Initialize(arguments);

		// With this mode, file data for resources will be kept around. This allows serializing embedded resource data inside scenes
		vz::resourcemanager::SetMode(vz::resourcemanager::Mode::ALLOW_RETAIN_FILEDATA);

		vz::backlog::setFontColor(vz::Color(130, 210, 220, 255));

		return VZ_OK;
	}

	VZRESULT DeinitEngineLib()
	{
		vz::jobsystem::ShutDown();
		return VZ_OK;
	}

	VID GetIdByName(const std::string& name)
	{
		return sceneManager.GetVidByName(name);
	}

	bool GetNameById(const VID vid, std::string& name)
	{
		name = sceneManager.GetNameByVid(vid);
		return name != "";
	}

	void RemoveEntity(const VID vid)
	{
		sceneManager.RemoveEntity(vid);
	}

	VID NewScene(const std::string& sceneName)
	{
		Scene* scene = sceneManager.GetSceneByName(sceneName);
		if (scene != nullptr)
		{
			return INVALID_ENTITY;
		}

		return sceneManager.CreateSceneEntity(sceneName);
	}

	void MoveToParent(const Entity entity, const Entity parentEntity, Scene* scene)
	{
		assert(entity != parentEntity);
		if (parentEntity != INVALID_ENTITY)
		{
			for (auto& entry : scene->componentLibrary.entries)
			{
				if (entry.second.component_manager->Contains(parentEntity))
				{
					scene->hierarchy.Create(entity).parentID = parentEntity;
					return;
				}
			}
		}
	}

	VID NewActor(const VID sceneId, const std::string& actorName, const VID parentId, VmActor** actor)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return INVALID_ENTITY;
		}
		Entity ett = scene->Entity_CreateObject(actorName);
		{
			VmActor* _actor = sceneManager.CreateVmComp<VmActor>(ett);
			if (actor) *actor = _actor;
		}
		MoveToParent(ett, parentId, scene);
		return ett;
	}

	VID NewCamera(const VID sceneId, const std::string& camName, const VID parentId, VmCamera** camera)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return INVALID_ENTITY;
		}
		Entity ett = scene->Entity_CreateCamera(camName, CANVAS_INIT_W, CANVAS_INIT_H);

		CameraComponent* camComponent = scene->cameras.GetComponent(ett);

		// camComponent's Eye, At and Up are basically updated when applying transform
		// if it has no transform, then they are used for computing the transform
		// Entity_CreateCamera provides a transform component by default
		// So, here, we just use the lookAt interface (UpdateCamera) of the camComponent
		VzmRenderer* renderer = sceneManager.CreateRenderer(ett);
		renderer->scene = scene;
		assert(renderer->camera == camComponent);
		VmCamera vmCam;
		vmCam.COMP_TYPE = typeid(VmCamera).name();
		vmCam.componentVID = ett;
		vmCam.renderer = (VmRenderer*)renderer;
		renderer->UpdateVmCamera(&vmCam);
		renderer->init(CANVAS_INIT_W, CANVAS_INIT_H, CANVAS_INIT_DPI);
		renderer->Load(); // Calls renderer->Start()

		if (camera) *camera = renderer->GetVmCamera();

		MoveToParent(ett, parentId, scene);
		return ett;
	}

	VID NewLight(const VID sceneId, const std::string& lightName, const VID parentId, VmLight** light)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return INVALID_ENTITY;
		}
		Entity ett = scene->Entity_CreateLight(lightName);
		{
			VmLight* _light = sceneManager.CreateVmComp<VmLight>(ett);
			if (light) *light = _light;
		}
		MoveToParent(ett, parentId, scene);
		return ett;
	}

	VmCamera* GetCamera(const VID camId)
	{
		VzmRenderer* renderer = sceneManager.GetRenderer(camId);
		if (renderer == nullptr)
		{
			return nullptr;
		}
		return renderer->GetVmCamera();
	}

	uint32_t GetSceneCameraIDs(const VID sceneId, std::vector<VID>& camIds)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return 0u;
		}
		camIds = scene->cameras.GetEntityArray();
		return (uint32_t)camIds.size();
	}

	VmAnimation* GetAnimation(const VID aniId)
	{
		return sceneManager.GetVmComp<VmAnimation>(aniId);
	}

	uint32_t GetSceneAnimations(const VID sceneId, std::vector<VID>& aniIds)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return 0u;
		}
		aniIds = scene->animations.GetEntityArray();
		return (uint32_t)aniIds.size();
	}

	VID LoadMeshModel(const VID sceneId, const std::string& file, const std::string& rootName)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return INVALID_ENTITY;
		}

		Entity rootEntity = INVALID_ENTITY; //sceneManager.internalResArchive.Entity_CreateMesh(actorName);
		// loading.. with file

		std::string extension = vz::helper::toUpper(vz::helper::GetExtensionFromFileName(file));
		FileType type = FileType::INVALID;
		auto it = filetypes.find(extension);
		if (it != filetypes.end())
		{
			type = it->second;
		}
		if (type == FileType::INVALID)
			return INVALID_ENTITY;

		Scene sceneTmp;
		vz::vector<Entity> camEntities;
		vz::vector<Entity> aniEntities, objEntities, lightEntities;
		if (type == FileType::OBJ) // wavefront-obj
		{
			rootEntity = ImportModel_OBJ(file, sceneTmp);	// reassign transform components
		}
		else if (type == FileType::GLTF || type == FileType::GLB || type == FileType::VRM) // gltf, vrm
		{
			rootEntity = ImportModel_GLTF(file, sceneTmp);
			camEntities = sceneTmp.cameras.GetEntityArray();

			aniEntities = sceneTmp.animations.GetEntityArray();
			objEntities = sceneTmp.lights.GetEntityArray();
			lightEntities = sceneTmp.objects.GetEntityArray();
		}
		sceneTmp.names.GetComponent(rootEntity)->name = rootName;
		scene->Merge(sceneTmp);


		for (Entity& ett : camEntities)
		{
			VzmRenderer* renderer = sceneManager.CreateRenderer(ett);
			CameraComponent* camComponent = scene->cameras.GetComponent(ett);
			renderer->scene = scene;
			assert(renderer->camera == camComponent);

			VmCamera vmCam;
			vmCam.COMP_TYPE = typeid(VmCamera).name();
			vmCam.componentVID = ett;
			vmCam.renderer = (VmRenderer*)renderer;

			//scene->hierarchy.Remove(ett);

			renderer->UpdateVmCamera(&vmCam);
			renderer->init(CANVAS_INIT_W, CANVAS_INIT_H, CANVAS_INIT_DPI);
			renderer->Load(); // Calls renderer->Start()
		}

		for (Entity& ett : aniEntities)
		{
			sceneManager.CreateVmComp<VmAnimation>(ett);
		}
		for (Entity& ett : objEntities)
		{
			sceneManager.CreateVmComp<VmActor>(ett);
		}
		for (Entity& ett : lightEntities)
		{
			sceneManager.CreateVmComp<VmLight>(ett);
		}

		return rootEntity;
	}

	VZRESULT Render(const int camId)
	{
		VzmRenderer* renderer = sceneManager.GetRenderer(camId);
		if (renderer == nullptr)
		{
			return VZ_FAIL;
		}

		vz::font::UpdateAtlas(renderer->GetDPIScaling());

		renderer->UpdateVmCamera();

		if (!vz::initializer::IsInitializeFinished())
		{
			// Until engine is not loaded, present initialization screen...
			renderer->WaitRender();
			return VZ_JOB_WAIT;
		}


		//{
		//	//Entity ett= vz::scene::GetScene().Entity_FindByName("cam transform");
		//	TransformComponent* transform = renderer->scene->transforms.GetComponent(renderer->camEntity);
		//	XMMATRIX matT = XMMatrixTranslation(0, 0.5f, -4.5f);
		//	static float ii = 0.f;
		//	XMMATRIX matR = XMMatrixRotationY(XM_PI / 180.f * (ii++));
		//	transform->ClearTransform();
		//	transform->MatrixTransform(matT * matR);
		//	transform->UpdateTransform();
		//	//vz::scene::GetCamera().TransformCamera(*transform);
		//}


		vz::profiler::BeginFrame();
		renderer->deltaTime = float(std::max(0.0, renderer->timer.record_elapsed_seconds()));

		const float target_deltaTime = 1.0f / renderer->targetFrameRate;
		if (renderer->framerate_lock && renderer->deltaTime < target_deltaTime)
		{
			vz::helper::QuickSleep((target_deltaTime - renderer->deltaTime) * 1000);
			renderer->deltaTime += float(std::max(0.0, renderer->timer.record_elapsed_seconds()));
		}
		//vz::input::Update(nullptr, *renderer);
		// Wake up the events that need to be executed on the main thread, in thread safe manner:
		vz::eventhandler::FireEvent(vz::eventhandler::EVENT_THREAD_SAFE_POINT, 0);
		renderer->fadeManager.Update(renderer->deltaTime);

		renderer->PreUpdate(); // current to previous

		// Fixed time update:
		{
			auto range = vz::profiler::BeginRangeCPU("Fixed Update");
			if (renderer->frameskip)
			{
				renderer->deltaTimeAccumulator += renderer->deltaTime;
				if (renderer->deltaTimeAccumulator > 10)
				{
					// application probably lost control, fixed update would take too long
					renderer->deltaTimeAccumulator = 0;
				}

				const float targetFrameRateInv = 1.0f / renderer->targetFrameRate;
				while (renderer->deltaTimeAccumulator >= targetFrameRateInv)
				{
					renderer->FixedUpdate();
					renderer->deltaTimeAccumulator -= targetFrameRateInv;
				}
			}
			else
			{
				renderer->FixedUpdate();
			}
			vz::profiler::EndRange(range); // Fixed Update
		}

		{
			auto range = vz::profiler::BeginRangeCPU("Update");
			vz::backlog::Update(*renderer, renderer->deltaTime);
			renderer->Update(renderer->deltaTime);
			renderer->PostUpdate();
			vz::profiler::EndRange(range); // Update
		}

		{
			auto range = vz::profiler::BeginRangeCPU("Render");
			renderer->Render();
			vz::profiler::EndRange(range); // Render
		}
		renderer->RenderFinalize();

		return VZ_OK;
	}

	void* TEST()
	{
		return vz::graphics::GetDevice();
	}

	void* GetGraphicsSharedRenderTarget(const int camId, const void* graphicsDev2, const void* srv_desc_heap2, const int descriptor_index, uint32_t* w, uint32_t* h)
	{
		VzmRenderer* renderer = sceneManager.GetRenderer(camId);
		if (renderer == nullptr)
		{
			return nullptr;
		}

		if (w) *w = renderer->width;
		if (h) *h = renderer->height;

		vz::graphics::GraphicsDevice* graphicsDevice = vz::graphics::GetDevice();
		//return graphicsDevice->OpenSharedResource(graphicsDev2, const_cast<vz::graphics::Texture*>(&renderer->GetRenderResult()));
		//return graphicsDevice->OpenSharedResource(graphicsDev2, &renderer->rtPostprocess);
		return graphicsDevice->OpenSharedResource(graphicsDev2, srv_desc_heap2, descriptor_index, const_cast<vz::graphics::Texture*>(&renderer->renderResult));
	}
}

#include "../vzArcBall.h"

namespace vzm
{
	std::unordered_map<ArcBall*, arcball::ArcBall> map_arcballs;

	ArcBall::ArcBall()
	{
		map_arcballs[this] = arcball::ArcBall();
	}
	ArcBall::~ArcBall()
	{
		map_arcballs.erase(this);
	}
	bool ArcBall::Intializer(const float stage_center[3], const float stage_radius)
	{
		auto itr = map_arcballs.find(this);
		if (itr == map_arcballs.end())
			return fail_ret("NOT AVAILABLE ARCBALL!");
		arcball::ArcBall& arc_ball = itr->second;
		XMVECTOR _stage_center = XMLoadFloat3((XMFLOAT3*)stage_center);
		arc_ball.FitArcballToSphere(_stage_center, stage_radius);
		arc_ball.__is_set_stage = true;
		return true;
	}
	void compute_screen_matrix(XMMATRIX& ps2ss, const float w, const float h)
	{
		XMMATRIX matTranslate = XMMatrixTranslation(1.f, -1.f, 0.f);
		XMMATRIX matScale = XMMatrixScaling(0.5f * w, -0.5f * h, 1.f);

		XMMATRIX matTranslateSampleModel = XMMatrixTranslation(-0.5f, -0.5f, 0.f);

		ps2ss = matTranslate * matScale * matTranslateSampleModel;
	}
	bool ArcBall::Start(const int pos_xy[2], const float screen_size[2],
		const float pos[3], const float view[3], const float up[3],
		const float np, const float fp, const float sensitivity)
	{
		auto itr = map_arcballs.find(this);
		if (itr == map_arcballs.end())
			return fail_ret("NOT AVAILABLE ARCBALL!");
		arcball::ArcBall& arc_ball = itr->second;
		if (!arc_ball.__is_set_stage)
			return fail_ret("NO INITIALIZATION IN THIS ARCBALL!");

		arcball::CameraState cam_pose_ac;
		cam_pose_ac.isPerspective = true;
		cam_pose_ac.np = np;
		cam_pose_ac.posCamera = XMFLOAT3(pos);
		cam_pose_ac.vecView = XMFLOAT3(view);

		// up vector correction
		XMVECTOR _view = XMLoadFloat3((XMFLOAT3*)view);
		XMVECTOR _up = XMLoadFloat3((XMFLOAT3*)up);
		XMVECTOR _right = XMVector3Cross(_view, _up);
		_up = XMVector3Normalize(XMVector3Cross(_right, _view));
		XMStoreFloat3(&cam_pose_ac.vecUp, _up);

		XMMATRIX ws2cs, cs2ps, ps2ss;
		ws2cs = VZMatrixLookTo(XMLoadFloat3(&cam_pose_ac.posCamera),
			XMLoadFloat3(&cam_pose_ac.vecView), XMLoadFloat3(&cam_pose_ac.vecUp));
		cs2ps = VZMatrixPerspectiveFov(XM_PIDIV4, (float)screen_size[0] / (float)screen_size[1], np, fp);
		compute_screen_matrix(ps2ss, screen_size[0], screen_size[1]);
		cam_pose_ac.matWS2SS = ws2cs * cs2ps * ps2ss;
		cam_pose_ac.matSS2WS = XMMatrixInverse(NULL, cam_pose_ac.matWS2SS);

		arc_ball.StartArcball((float)pos_xy[0], (float)pos_xy[1], cam_pose_ac, 10.f * sensitivity);

		return true;
	}
	bool ArcBall::Move(const int pos_xy[2], float mat_r_onmove[16])
	{
		auto itr = map_arcballs.find(this);
		if (itr == map_arcballs.end())
			return fail_ret("NOT AVAILABLE ARCBALL!");
		arcball::ArcBall& arc_ball = itr->second;
		if (!arc_ball.__is_set_stage)
			return fail_ret("NO INITIALIZATION IN THIS ARCBALL!");

		XMMATRIX mat_tr;
		arc_ball.MoveArcball(mat_tr, (float)pos_xy[0], (float)pos_xy[1], false); // cf. true
		*(XMMATRIX*)mat_r_onmove = XMMatrixTranspose(mat_tr);
		return true;
	}
	bool ArcBall::Move(const int pos_xy[2], float pos[3], float view[3], float up[3])
	{
		auto itr = map_arcballs.find(this);
		if (itr == map_arcballs.end())
			return fail_ret("NOT AVAILABLE ARCBALL!");
		arcball::ArcBall& arc_ball = itr->second;
		if (!arc_ball.__is_set_stage)
			return fail_ret("NO INITIALIZATION IN THIS ARCBALL!");

		XMMATRIX mat_tr;
		arc_ball.MoveArcball(mat_tr, (float)pos_xy[0], (float)pos_xy[1], true);

		arcball::CameraState cam_pose_begin = arc_ball.GetCameraStateSetInStart();
		XMVECTOR pos_eye = XMVector3TransformCoord(XMLoadFloat3(&cam_pose_begin.posCamera), mat_tr);
		XMVECTOR vec_view = XMVector3TransformNormal(XMLoadFloat3(&cam_pose_begin.vecView), mat_tr);
		XMVECTOR vec_up = XMVector3TransformNormal(XMLoadFloat3(&cam_pose_begin.vecUp), mat_tr);

		XMStoreFloat3((XMFLOAT3*)pos, pos_eye);
		XMStoreFloat3((XMFLOAT3*)view, XMVector3Normalize(vec_view));
		XMStoreFloat3((XMFLOAT3*)up, XMVector3Normalize(vec_up));

		return true;
	}
	bool ArcBall::PanMove(const int pos_xy[2], float pos[3], float view[3], float up[3])
	{
		auto itr = map_arcballs.find(this);
		if (itr == map_arcballs.end())
			return fail_ret("NOT AVAILABLE ARCBALL!");
		arcball::ArcBall& arc_ball = itr->second;
		if (!arc_ball.__is_set_stage)
			return fail_ret("NO INITIALIZATION IN THIS ARCBALL!");

		arcball::CameraState cam_pose_begin = arc_ball.GetCameraStateSetInStart();

		XMMATRIX& mat_ws2ss = cam_pose_begin.matWS2SS;
		XMMATRIX& mat_ss2ws = cam_pose_begin.matSS2WS;

		if (!cam_pose_begin.isPerspective)
		{
			XMVECTOR pos_eye_ws = XMLoadFloat3(&cam_pose_begin.posCamera);
			XMVECTOR pos_eye_ss = XMVector3TransformCoord(pos_eye_ws, mat_ws2ss);

			XMFLOAT3 v = XMFLOAT3((float)pos_xy[0] - arc_ball.__start_x, (float)pos_xy[1] - arc_ball.__start_y, 0);
			XMVECTOR diff_ss = XMLoadFloat3(&v);

			pos_eye_ss = pos_eye_ss - diff_ss; // Think Panning! reverse camera moving
			pos_eye_ws = XMVector3TransformCoord(pos_eye_ss, mat_ss2ws);

			XMStoreFloat3((XMFLOAT3*)pos, pos_eye_ws);
			*(XMFLOAT3*)up = cam_pose_begin.vecUp;
			*(XMFLOAT3*)view = cam_pose_begin.vecView;
		}
		else
		{
			XMFLOAT3 f = XMFLOAT3((float)pos_xy[0], (float)pos_xy[1], 0);
			XMVECTOR pos_cur_ss = XMLoadFloat3(&f);
			f = XMFLOAT3(arc_ball.__start_x, arc_ball.__start_y, 0);
			XMVECTOR pos_old_ss = XMLoadFloat3(&f);
			XMVECTOR pos_cur_ws = XMVector3TransformCoord(pos_cur_ss, mat_ss2ws);
			XMVECTOR pos_old_ws = XMVector3TransformCoord(pos_old_ss, mat_ss2ws);
			XMVECTOR diff_ws = pos_cur_ws - pos_old_ws;

			if (XMVectorGetX(XMVector3Length(diff_ws)) < DBL_EPSILON)
			{
				*(XMFLOAT3*)pos = cam_pose_begin.posCamera;
				*(XMFLOAT3*)up = cam_pose_begin.vecUp;
				*(XMFLOAT3*)view = cam_pose_begin.vecView;
				return true;
			}

			//cout << "-----0> " << glm::length(diff_ws) << endl;
			//cout << "-----1> " << pos.x << ", " << pos.y << endl;
			//cout << "-----2> " << arc_ball.__start_x << ", " << arc_ball.__start_y << endl;
			XMVECTOR pos_center_ws = arc_ball.GetCenterStage();
			XMVECTOR vec_eye2center_ws = pos_center_ws - XMLoadFloat3(&cam_pose_begin.posCamera);

			float panningCorrected = XMVectorGetX(XMVector3Length(diff_ws)) * XMVectorGetX(XMVector3Length(vec_eye2center_ws)) / cam_pose_begin.np;

			diff_ws = XMVector3Normalize(diff_ws);
			XMVECTOR v = XMLoadFloat3(&cam_pose_begin.posCamera) - XMVectorScale(diff_ws, panningCorrected);
			XMStoreFloat3((XMFLOAT3*)pos, v);
			*(XMFLOAT3*)up = cam_pose_begin.vecUp;
			*(XMFLOAT3*)view = cam_pose_begin.vecView;
		}
		return true;
	}
}
