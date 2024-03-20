#include "VizEngineAPIs.h"
#include "vzEngine.h"

#include "vzGraphicsDevice_DX12.h"
#include "vzGraphicsDevice_Vulkan.h"

using namespace vz::ecs;
using namespace vz::scene;

namespace vz 
{
	class VzmRenderer : public vz::RenderPath3D
	{
	public:
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

		void Update(float dt) override
		{
			RenderPath3D::Update(dt);
		}
	};

	static inline constexpr Entity INVALID_SCENE_ENTITY = 0;
	class ApiManager
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
				Scene& scene = scenes[next];
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

namespace vmath 
{
	void vmath::TransformPoint(const float* pos_src, const float* mat, const bool is_rowMajor, float* pos_dst)
	{

	}
	void vmath::TransformVector(const float* vec_src, const float* mat, const bool is_rowMajor, float* vec_dst)
	{

	}
	void vmath::ComputeBoxTransformMatrix(const float* cube_scale, const float* pos_center, const float* y_axis, const float* z_axis, const bool is_rowMajor, float* mat_tr, float* inv_mat_tr)
	{

	}
}

namespace vzm
{
	vz::ApiManager vzmApp;

	VZRESULT InitEngineLib(const std::string& coreName, const std::string& logFileName)
	{
		static bool initialized = false;
		if (initialized)
		{
			return VZ_OK;
		}

		ParamMap<std::string> arguments;
		vzmApp.Initialize(arguments);

		return VZ_OK;
	}

	VZRESULT DeinitEngineLib()
	{
		vz::jobsystem::ShutDown();
		return VZ_OK;
	}

	VID NewScene(const std::string& sceneName)
	{
		Scene* scene = vzmApp.GetSceneByName(sceneName);
		if (scene != nullptr)
		{
			return INVALID_ENTITY;
		}
		return vzmApp.CreateSceneEntity(sceneName);
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
		Scene* scene = vzmApp.GetScene(sceneId);
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
		Scene* scene = vzmApp.GetScene(sceneId);
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
		vzmApp.CreateRenderer(ett);
		MoveToParent(ett, parentId, scene);
		return ett;
	}

	VID NewLight(const VID sceneId, const std::string& lightName, const LightParameter& lParams, const VID parentId)
	{
		Scene* scene = vzmApp.GetScene(sceneId);
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
		return vzmApp.internalResArchive.Entity_CreateMesh(file);
	}

	VZRESULT RenderScene(const int sceneId, const int camId)
	{
		vz::font::UpdateAtlas(vzmApp.canvas.GetDPIScaling());

		//ColorSpace colorspace = graphicsDevice->GetSwapChainColorSpace(&swapChain);

		if (!vz::initializer::IsInitializeFinished())
		{
			// Until engine is not loaded, present initialization screen...
			vz::graphics::CommandList cmd = graphicsDevice->BeginCommandList();
			graphicsDevice->RenderPassBegin(&swapChain, cmd);
			vz::graphics::Viewport viewport;
			viewport.width = (float)swapChain.desc.width;
			viewport.height = (float)swapChain.desc.height;
			graphicsDevice->BindViewports(1, &viewport, cmd);
			if (vz::initializer::IsInitializeFinished(vz::initializer::INITIALIZED_SYSTEM_FONT))
			{
				vz::backlog::DrawOutputText(canvas, cmd, colorspace);
			}
			graphicsDevice->RenderPassEnd(cmd);
			graphicsDevice->SubmitCommandLists();
			return VZ_JOB_WAIT;
		}

		vz::profiler::BeginFrame();

		return VZ_OK;
	}
}