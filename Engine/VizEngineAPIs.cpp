#include "VizEngineAPIs.h"
#include "vzEngine.h"

using namespace vz::ecs;
using namespace vz::scene;

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

VzmRenderer g_renderer;

VZRESULT vzm::InitEngineLib(const std::string& coreName, const std::string& logFileName)
{
	static bool initialized = false;
	if (initialized)
	{
		return VZ_OK;
	}

	vz::initializer::InitializeComponentsAsync();
	vz::backlog::Toggle(); // set backlog enable to true
	return VZ_OK;
}

VZRESULT vzm::DeinitEngineLib()
{
	return VZ_OK;
}