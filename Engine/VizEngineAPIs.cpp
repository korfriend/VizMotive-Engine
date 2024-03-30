#include "VizEngineAPIs.h"
#include "vzEngine.h"

#include "vzGraphicsDevice_DX12.h"
#include "vzGraphicsDevice_Vulkan.h"

using namespace vz::ecs;
using namespace vz::scene;

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
		XMMATRIX ws2cs = XMMatrixLookToRH(pos_eye, vec_view, vec_up);
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

	public:
		std::unique_ptr<CameraParams> cParam;
		TimeStamp cParam_timeStamp = std::chrono::high_resolution_clock::now();
		vz::scene::TransformComponent* transform = nullptr;

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

		void ResizeRenderTargets()
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
						desc.format = vz::graphics::Format::R11G11B10_FLOAT;
						desc.bind_flags = vz::graphics::BindFlag::RENDER_TARGET | vz::graphics::BindFlag::SHADER_RESOURCE;
						if (!isInterResult)
						{
							desc.misc_flags = vz::graphics::ResourceMiscFlag::SHARED;
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

		void Load() override	// loading image
		{
			setSSREnabled(false);
			setReflectionsEnabled(true);
			setFXAAEnabled(false);

			this->ClearSprites();
			this->ClearFonts();

			RenderPath3D::Load();

			gridHelper = false;
			infoDisplay.active = true;
			infoDisplay.watermark = true;
			infoDisplay.fpsinfo = true;
			infoDisplay.resolution = true;
			infoDisplay.heap_allocation_counter = true;

			vz::font::UpdateAtlas(GetDPIScaling());
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
					infodisplay_str += "VizMotive Engine ";
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
					ColorSpace colorSpace = graphicsDevice->GetSwapChainColorSpace(&swapChain);
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
#ifdef WICKED_ENGINE_HEAP_ALLOCATION_COUNTER
					infodisplay_str += std::to_string(number_of_heap_allocations.load());
					infodisplay_str += " (";
					infodisplay_str += std::to_string(size_of_heap_allocations.load());
					infodisplay_str += " bytes)\n";
					number_of_heap_allocations.store(0);
					size_of_heap_allocations.store(0);
#else
					infodisplay_str += "[disabled]\n";
#endif // WICKED_ENGINE_HEAP_ALLOCATION_COUNTER
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
						RenderPassImage::RenderTarget(&renderResult, RenderPassImage::LoadOp::CLEAR),
					};
					graphicsDevice->RenderPassBegin(rp, arraysize(rp), cmd);
				}
			}

			Compose(cmd);

			graphicsDevice->RenderPassEnd(cmd);
			if (colorspace_conversion_required)
			{
				// In HDR10, we perform a final mapping from linear to HDR10, into the swapchain
				graphicsDevice->RenderPassBegin(&swapChain, cmd);
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

		void UpdateCParams()
		{
			if (cParam == nullptr)
			{
				return;
			}
			std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(cParam_timeStamp - cParam->timeStamp);
			if (time_span.count() >= 0)
			{
				return;
			}

			camera->Eye = *(XMFLOAT3*)cParam->pos;
			camera->At = XMFLOAT3(cParam->pos[0] + cParam->view[0], cParam->pos[1] + cParam->view[1], cParam->pos[2] + cParam->view[2]);

			// up vector correction
			XMVECTOR _view = XMLoadFloat3((XMFLOAT3*)cParam->view);
			XMVECTOR _up = XMLoadFloat3((XMFLOAT3*)cParam->up);
			XMVECTOR _right = XMVector3Cross(_view, _up);
			_up = XMVector3Normalize(XMVector3Cross(_right, _view));
			XMStoreFloat3(&camera->Up, _up);

			camera->UpdateCamera();
			transform->MatrixTransform(camera->GetView());
			init(cParam->w, cParam->h, cParam->dpi);

			cParam_timeStamp = std::chrono::high_resolution_clock::now();
		}
	};

	class SceneManager
	{
	private:
		std::unique_ptr<vz::graphics::GraphicsDevice> graphicsDevice;

		unordered_map<VID, Scene> scenes;						// <SceneEntity, Scene>
		// one camera component to one renderer
		unordered_map<VID, VzmRenderer> renderers;				// <CamEntity, VzmRenderer> (including camera and scene)

		unordered_map<std::string, VID> nameMap;				// not allowed redundant name


	public:
		Scene internalResArchive;

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
				Scene& scene = scenes[ett];
				scene.weather = WeatherComponent();
				nameMap[name] = ett;
			}
			return ett;
		}
		inline void RemoveScene(Entity sid)
		{
			Scene* scene = GetScene(sid);
			if (scene)
			{
				scenes.erase(sid);
				for (auto it : nameMap)
				{
					if (sid == it.second)
					{
						nameMap.erase(it.first);
						return;
					}
				}
			};
		}
		inline VID GetVidByName(const std::string& name)
		{
			auto it = nameMap.find(name);
			if (it == nameMap.end())
			{
				return INVALID_ENTITY;
			}
			return it->second;
		}
		inline Scene* GetScene(const VID sid)
		{
			auto it = scenes.find(sid);
			if (it == scenes.end())
			{
				return nullptr;
			}
			return &it->second;
		}
		inline Scene* GetSceneByName(const std::string& name) 
		{
			return GetScene(GetVidByName(name));
		}
		inline unordered_map<VID, Scene>& GetScenes()
		{
			return scenes;
		}
		inline VzmRenderer* CreateRenderer(const VID camEntity)
		{
			auto it = renderers.find(camEntity);
			assert(it == renderers.end());

			VzmRenderer* renderer = &renderers[camEntity];

			for (auto it = scenes.begin(); it != scenes.end(); it++)
			{
				Scene* scene = &it->second;
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
		inline void RemoveRenderer(Entity camEntity)
		{
			renderers.erase(camEntity);
			for (auto it : nameMap)
			{
				if (camEntity == it.second)
				{
					nameMap.erase(it.first);
					return;
				}
			}
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
			
			// Reset all state that tests might have modified:
			vz::eventhandler::SetVSync(true);
			vz::renderer::SetToDrawGridHelper(false);
			vz::renderer::SetTemporalAAEnabled(false);
			vz::renderer::ClearWorld(vz::scene::GetScene());
		}
	};
}

namespace vzm
{
	SceneManager sceneManager;

	VZRESULT InitEngineLib(const std::string& coreName, const std::string& logFileName)
	{
		static bool initialized = false;
		if (initialized)
		{
			return VZ_OK;
		}

		ParamMap<std::string> arguments;
		sceneManager.Initialize(arguments);

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

	VID NewActor(const VID sceneId, const std::string& actorName, const ActorParams& aParams, const VID parentId)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return INVALID_ENTITY;
		}
		Entity ett = scene->Entity_CreateObject(actorName);
		// TO DO
		{
			ObjectComponent* objComponent = scene->objects.GetComponent(ett);
			objComponent->meshID = aParams.GetResourceID(ActorParams::RES_USAGE::GEOMETRY);
		}
		MoveToParent(ett, parentId, scene);
		return ett;
	}

	// cameraComponent, renderer, ==> CameraParams
	// objectComponent ==> ActorParams
	// lightCompoenent ==> LightParams

	VID NewCamera(const VID sceneId, const std::string& camName, const CameraParams& cParams, const VID parentId)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return INVALID_ENTITY;
		}
		Entity ett = INVALID_ENTITY;
		switch (cParams.projectionMode)
		{
		case CameraParams::ProjectionMode::CAMERA_FOV:
			ett = scene->Entity_CreateCamera(camName, (float)cParams.w, (float)cParams.h, cParams.np, cParams.fp, cParams.fov_y);
			break;
		case CameraParams::ProjectionMode::CAMERA_INTRINSICS:
		case CameraParams::ProjectionMode::IMAGEPLANE_SIZE:
		case CameraParams::ProjectionMode::SLICER_PLANE:
		case CameraParams::ProjectionMode::SLICER_CURVED:
		default:
			return INVALID_ENTITY;
		}

		CameraComponent* camComponent = scene->cameras.GetComponent(ett);
		assert(camComponent);
		TransformComponent* transform = scene->transforms.GetComponent(ett);
		//transform->Translate(XMFLOAT3(10, 20.f, -40.5f));
		//transform->UpdateTransform();
		
		// camComponent's Eye, At and Up are basically updated when applying transform
		// if it has no transform, then they are used for computing the transform
		// Entity_CreateCamera provides a transform component by default
		// So, here, we just use the lookAt interface (UpdateCamera) of the camComponent
		VzmRenderer* renderer = sceneManager.CreateRenderer(ett);
		renderer->camera = camComponent;
		renderer->transform = transform;
		renderer->scene = scene;
		renderer->cParam = std::make_unique<CameraParams>(cParams);
		renderer->cParam->timeStamp = std::chrono::high_resolution_clock::now();
		renderer->UpdateCParams();
		renderer->Start(); // call ResizeBuffers();
		renderer->Load();
		MoveToParent(ett, parentId, scene);
		return ett;
	}

	VID NewLight(const VID sceneId, const std::string& lightName, const LightParams& lParams, const VID parentId)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return INVALID_ENTITY;
		}
		Entity ett = scene->Entity_CreateLight(lightName);
		MoveToParent(ett, parentId, scene);
		return ett;
	}

	CameraParams* GetCamera(const VID camId)
	{
		VzmRenderer* renderer = sceneManager.GetRenderer(camId);
		if (renderer == nullptr)
		{
			return nullptr;
		}
		renderer->cParam->timeStamp = std::chrono::high_resolution_clock::now();
		return renderer->cParam.get();
	}

	void UpdateCamera(const VID camId)
	{
		VzmRenderer* renderer = sceneManager.GetRenderer(camId);
		if (renderer)
		{
			renderer->UpdateCParams();
		}
		
		return;
	}

	VID LoadMeshModel(const std::string& file, const std::string& resName)
	{
		Entity ett = sceneManager.internalResArchive.Entity_CreateMesh(resName);
		// loading.. with file
		return ett;
	}

	VZRESULT Render(const int camId)
	{
		VzmRenderer* renderer = sceneManager.GetRenderer(camId);
		if (renderer == nullptr)
		{
			return VZ_FAIL;
		}
		
		vz::font::UpdateAtlas(renderer->GetDPIScaling());

		vz::graphics::GraphicsDevice* graphicsDevice = vz::graphics::GetDevice();

		renderer->ResizeRenderTargets();

		if (!vz::initializer::IsInitializeFinished())
		{
			// Until engine is not loaded, present initialization screen...
			renderer->WaitRender();
			return VZ_JOB_WAIT;
		}

		// remove...
		vz::renderer::SetToDrawGridHelper(true);

		vz::profiler::BeginFrame();
		renderer->deltaTime = float(std::max(0.0, renderer->timer.record_elapsed_seconds()));

		const float target_deltaTime = 1.0f / renderer->targetFrameRate;
		if (renderer->framerate_lock && renderer->deltaTime < target_deltaTime)
		{
			vz::helper::QuickSleep((target_deltaTime - renderer->deltaTime) * 1000);
			renderer->deltaTime += float(std::max(0.0, renderer->timer.record_elapsed_seconds()));
		}
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

	void* GetGraphicsSharedRenderTarget(const int camId, const void* graphicsDev2, uint32_t* w, uint32_t* h)
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
		//return graphicsDevice->OpenSharedResource(graphicsDev2, &renderer->rtMain);
		return graphicsDevice->OpenSharedResource(graphicsDev2, const_cast<vz::graphics::Texture*>(&renderer->renderResult));
	}
}

#include "vzArcBall.h"

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
		XMVECTOR dstage_center = XMLoadFloat3((XMFLOAT3*)stage_center);
		arc_ball.FitArcballToSphere(dstage_center, stage_radius);
		arc_ball.__is_set_stage = true;
		return true;
	}
	void compute_screen_matrix(XMMATRIX& ps2ss, const float w, const float h)
	{
		XMMATRIX matTranslate = XMMatrixTranslation(1.f, -1.f, 0.f);
		XMMATRIX matScale = XMMatrixScaling(0.5f * w, -0.5f * h, 0.5f);

		XMMATRIX matTranslateSampleModel = XMMatrixTranslation(-0.5f, -0.5f, 0.f);

		ps2ss = XMMatrixMultiply(XMMatrixMultiply(matTranslate, matScale), matTranslateSampleModel);
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
		cam_pose_ac.posCamera = XMFLOAT3(pos);
		cam_pose_ac.vecView = XMFLOAT3(view);
		cam_pose_ac.vecUp = XMFLOAT3(up);
		cam_pose_ac.np = np;

		XMMATRIX ws2cs, cs2ps, ps2ss;
		ws2cs = XMMatrixLookToRH(XMLoadFloat3(&cam_pose_ac.posCamera), 
			XMLoadFloat3(&cam_pose_ac.vecView), XMLoadFloat3(&cam_pose_ac.vecUp));
		cs2ps = XMMatrixPerspectiveFovRH(XM_PIDIV4, (float)screen_size[0] / (float)screen_size[1], np, fp);
		compute_screen_matrix(ps2ss, screen_size[0], screen_size[1]);
		cam_pose_ac.matWS2SS = XMMatrixMultiply(XMMatrixMultiply(ws2cs, cs2ps), ps2ss);
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