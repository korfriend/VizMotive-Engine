#pragma once
#include "vzEngine.h"


class Example_ImGuiRenderer : public vz::RenderPath3D
{
	vz::gui::Label label;
public:
	void Load() override;
	void Update(float dt) override;
	void ResizeLayout() override;
	void Render() const override;
	void DisplayPerformanceData(bool* p_open);
	void igTextTitle(const char* text);
};

class Example_ImGui : public vz::Application
{
	Example_ImGuiRenderer renderer;

public:
	~Example_ImGui() override;
	void Initialize() override;
	void Compose(vz::graphics::CommandList cmd) override;
};

