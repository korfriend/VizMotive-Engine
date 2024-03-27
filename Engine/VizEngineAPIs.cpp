#include "VizEngineAPIs.h"
#include "vzEngine.h"

#include "vzGraphicsDevice_DX12.h"
#include "vzGraphicsDevice_Vulkan.h"

using namespace vz::ecs;
using namespace vz::scene;

namespace vzm
{
	void TransformPoint(const float* pos_src, const float* mat, const bool is_rowMajor, float* pos_dst)
	{

	}
	void TransformVector(const float* vec_src, const float* mat, const bool is_rowMajor, float* vec_dst)
	{

	}
	void ComputeBoxTransformMatrix(const float* cube_scale, const float* pos_center, const float* y_axis, const float* z_axis, const bool is_rowMajor, float* mat_tr, float* inv_mat_tr)
	{

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

		float deltaTime = 0;
		float deltaTimeAccumulator = 0;
		float targetFrameRate = 60;
		bool frameskip = true;
		bool framerate_lock = false;
		vz::Timer timer;
		int fps_avg_counter = 0;

		vz::FadeManager fadeManager;

		// render target 을 compose... 
		Entity camEntity;
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

		void UpdateRenderOutputRes()
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
				vz::graphics::RenderPassImage rt[] = { vz::graphics::RenderPassImage::RenderTarget(&renderResult) };
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
	};

	static inline constexpr Entity INVALID_SCENE_ENTITY = 0;
	class SceneManager
	{
	private:
		std::unique_ptr<vz::graphics::GraphicsDevice> graphicsDevice;

		unordered_map<VID, Scene> scenes;						// <SceneEntity, Scene>
		// one camera component to one renderer
		unordered_map<VID, VzmRenderer> renderers;				// <CamEntity, VzmRenderer> (including camera and scene)

		unordered_map<std::string, VID> sceneNames;		
		unordered_map<std::string, VID> cameraNames;


	public:
		Scene internalResArchive;

		// Runtime can create a new entity with this
		inline Entity CreateSceneEntity(const std::string& name)
		{
			static std::atomic<Entity> next{ INVALID_SCENE_ENTITY + 1 };

			auto sid = sceneNames.find(name);
			if (sid != sceneNames.end())
			{
				return INVALID_SCENE_ENTITY;
			}

			Entity ett = next.fetch_add(1);
			if (ett != INVALID_SCENE_ENTITY) {
				Scene& scene = scenes[ett];
				scene.weather = WeatherComponent();
				sceneNames[name] = next;
			}
			return ett;
		}
		inline void RemoveScene(Entity sid)
		{
			Scene* scene = GetScene(sid);
			if (scene)
			{
				scenes.erase(sid);
				for (auto it : sceneNames)
				{
					if (sid == it.second)
					{
						sceneNames.erase(it.first);
					}
				}
			};
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
			auto it = sceneNames.find(name);
			if (it == sceneNames.end())
			{
				return nullptr;
			}
			return GetScene(it->second);
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
					cameraNames[scene->names.GetComponent(camEntity)->name] = camEntity;
					return renderer;
				}
			}
			renderers.erase(camEntity);
			return nullptr;
		}
		inline void RemoveRenderer(Entity camEntity)
		{
			renderers.erase(camEntity);
			for (auto it : cameraNames)
			{
				if (camEntity == it.second)
				{
					cameraNames.erase(it.first);
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

	VID NewActor(const VID sceneId, const std::string& actorName, const ActorParameter& aParams, const VID parentId)
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
			objComponent->meshID = aParams.GetResourceID(ActorParameter::RES_USAGE::GEOMETRY);
		}
		MoveToParent(ett, parentId, scene);
		return ett;
	}

	VID NewCamera(const VID sceneId, const std::string& camName, const CameraParameter& cParams, const VID parentId)
	{
		Scene* scene = sceneManager.GetScene(sceneId);
		if (scene == nullptr)
		{
			return INVALID_ENTITY;
		}
		Entity ett = INVALID_ENTITY;
		switch (cParams.projectionMode)
		{
		case CameraParameter::ProjectionMode::CAMERA_FOV:
			ett = scene->Entity_CreateCamera(camName, (float)cParams.w, (float)cParams.h, cParams.np, cParams.fp, cParams.fov_y);
			break;
		case CameraParameter::ProjectionMode::CAMERA_INTRINSICS:
		case CameraParameter::ProjectionMode::IMAGEPLANE_SIZE:
		case CameraParameter::ProjectionMode::SLICER_PLANE:
		case CameraParameter::ProjectionMode::SLICER_CURVED:
		default:
			return INVALID_ENTITY;
		}

		TransformComponent transform;
		transform.Translate(XMFLOAT3(8, 8, 2));
		transform.UpdateTransform();

		CameraComponent* camComponent = scene->cameras.GetComponent(ett);
		assert(camComponent);
		camComponent->TransformCamera(transform);

		카메라 update...
		camComponent->Eye = XMFLOAT3(8, 8, 2);
		camComponent->UpdateCamera();

		VzmRenderer* renderer = sceneManager.CreateRenderer(ett);
		renderer->init(cParams.w, cParams.h);
		renderer->Start(); // call ResizeBuffers();
		renderer->Load();
		MoveToParent(ett, parentId, scene);
		return ett;
	}

	VID NewLight(const VID sceneId, const std::string& lightName, const LightParameter& lParams, const VID parentId)
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

	VID LoadMeshModel(const std::string& file, const std::string& resName)
	{
		return sceneManager.internalResArchive.Entity_CreateMesh(file);
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

		renderer->UpdateRenderOutputRes();

		if (!vz::initializer::IsInitializeFinished())
		{
			// Until engine is not loaded, present initialization screen...
			renderer->WaitRender();
			return VZ_JOB_WAIT;
		}

		vz::profiler::BeginFrame();

		vz::renderer::SetToDrawGridHelper(true);
		renderer->deltaTime = float(std::max(0.0, renderer->timer.record_elapsed_seconds()));
		renderer->PreUpdate(); // current to previous
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
		renderer->Update(renderer->deltaTime);
		renderer->Render();
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