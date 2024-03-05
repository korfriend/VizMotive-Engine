#include "vzLoadingScreen.h"
#include "vzApplication.h"

#include <thread>

namespace vz
{

	bool LoadingScreen::isActive()
	{
		return vz::jobsystem::IsBusy(ctx);
	}

	void LoadingScreen::addLoadingFunction(std::function<void(vz::jobsystem::JobArgs)> loadingFunction)
	{
		if (loadingFunction != nullptr)
		{
			tasks.push_back(loadingFunction);
		}
	}

	void LoadingScreen::addLoadingComponent(RenderPath* component, Application* main, float fadeSeconds, vz::Color fadeColor)
	{
		addLoadingFunction([=](vz::jobsystem::JobArgs args) {
			component->Load();
			});
		onFinished([=] {
			main->ActivatePath(component, fadeSeconds, fadeColor);
			});
	}

	void LoadingScreen::onFinished(std::function<void()> finishFunction)
	{
		if (finishFunction != nullptr)
			finish = finishFunction;
	}

	void LoadingScreen::Start()
	{
		for (auto& x : tasks)
		{
			vz::jobsystem::Execute(ctx, x);
		}
		std::thread([this]() {
			vz::jobsystem::Wait(ctx);
			finish();
			}).detach();

			RenderPath2D::Start();
	}

	void LoadingScreen::Stop()
	{
		tasks.clear();
		finish = nullptr;

		RenderPath2D::Stop();
	}

}
