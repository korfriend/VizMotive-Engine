#include "VizEngineAPIs.h"
#include "vzEngine.h"

using namespace vz::ecs;
using namespace vz::scene;

namespace vz 
{
	class VzmRenderer : public vz::RenderPath3D
	{
	public:
		void Load() override
		{
			setSSREnabled(false);
			setReflectionsEnabled(true);
			setFXAAEnabled(false);

			// Reset all state that tests might have modified:
			vz::eventhandler::SetVSync(true);
			vz::renderer::SetToDrawGridHelper(false);
			vz::renderer::SetTemporalAAEnabled(false);
			vz::renderer::ClearWorld(vz::scene::GetScene());
			vz::scene::GetScene().weather = WeatherComponent();
			this->ClearSprites();
			this->ClearFonts();

			// Reset camera position:
			TransformComponent transform;
			transform.Translate(XMFLOAT3(0, 2.f, -4.5f));
			transform.UpdateTransform();
			vz::scene::GetCamera().TransformCamera(transform);

			float screenW = GetLogicalWidth();
			float screenH = GetLogicalHeight();

			RenderPath3D::Load();
		}

		void Update(float dt) override
		{
			RenderPath3D::Update(dt);
		}
	};

	static inline constexpr Entity INVALID_SCENE_ENTITY = 0;
	class VzmApp : public vz::Application
	{
	private:
		unordered_map<VID, VzmRenderer> renderers;				// <CamEntity, VzmRenderer> (including camera and scene)
		unordered_map<VID, Scene> scenes;						// <SceneEntity, Scene>

		unordered_map<std::string, VID> sceneNames;		
		unordered_map<std::string, VID> cameraNames;		

	public:
		// Runtime can create a new entity with this
		inline Entity CreateSceneEntity(const std::string& name)
		{
			static std::atomic<Entity> next{ INVALID_SCENE_ENTITY + 1 };

			auto sid = sceneNames.find(name);
			if (sid != sceneNames.end())
			{
				return INVALID_SCENE_ENTITY;
			}

			scenes[next];
			sceneNames[name] = next;

			return next.fetch_add(1);
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

		void Initialize() override
		{
			// device creation
			SetWindow(NULL);

			vz::Application::Initialize();

			infoDisplay.active = true;
			infoDisplay.watermark = true;
			infoDisplay.fpsinfo = true;
			infoDisplay.resolution = true;
			infoDisplay.heap_allocation_counter = true;

			vz::font::UpdateAtlas(canvas.GetDPIScaling());
			while (false)
			{
				using namespace vz::graphics;
				//ColorSpace colorspace = graphicsDevice->GetSwapChainColorSpace(&swapChain);
				Sleep(2);
				if (!vz::initializer::IsInitializeFinished())
				{
					// Until engine is not loaded, present initialization screen...
					//CommandList cmd = graphicsDevice->BeginCommandList();
					//graphicsDevice->RenderPassBegin(&swapChain, cmd);
					//Viewport viewport;
					//viewport.width = (float)swapChain.desc.width;
					//viewport.height = (float)swapChain.desc.height;
					//graphicsDevice->BindViewports(1, &viewport, cmd);
					//if (vz::initializer::IsInitializeFinished(vz::initializer::INITIALIZED_SYSTEM_FONT))
					//{
					//	vz::backlog::DrawOutputText(canvas, cmd, colorspace);
					//}
					//graphicsDevice->RenderPassEnd(cmd);
					//graphicsDevice->SubmitCommandLists();
					break;
				}
			}
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
	vz::VzmApp vzmApp;

	VZRESULT InitEngineLib(const std::string& coreName, const std::string& logFileName)
	{
		static bool initialized = false;
		if (initialized)
		{
			return VZ_OK;
		}

		vzmApp.Initialize();

		return VZ_OK;
	}

	VZRESULT DeinitEngineLib()
	{
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

	VID NewCamera(const VID sceneId, const std::string& camName, const CameraParameters& cParams)
	{
		Scene* scene = vzmApp.GetScene(sceneId);
		if (scene == nullptr)
		{
			return INVALID_ENTITY;
		}

		Entity ety = INVALID_ENTITY;
		switch (cParams.projection_mode)
		{
		case CameraParameters::ProjectionMode::CAMERA_FOV:
			ety = scene->Entity_CreateCamera(camName, (float)cParams.w, (float)cParams.h, cParams.np, cParams.fp, cParams.fov_y);
			break;
		case CameraParameters::ProjectionMode::CAMERA_INTRINSICS:
		case CameraParameters::ProjectionMode::IMAGEPLANE_SIZE:
		case CameraParameters::ProjectionMode::SLICER_PLANE:
		case CameraParameters::ProjectionMode::SLICER_CURVED:
		default:
			return INVALID_ENTITY;
		}

		return ety;
	}
}