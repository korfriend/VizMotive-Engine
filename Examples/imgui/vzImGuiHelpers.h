#pragma once
// Filament highlevel APIs
#include "vzm2/VzEngineAPIs.h"
#include "vzm2/utils/Backlog.h"
#include "vzm2/utils/EventHandler.h"
#include "vzm2/utils/Profiler.h"
#include "vzm2/utils/JobSystem.h"
#include "vzm2/utils/Config.h"
#include "vzm2/utils/vzMath.h"

#include "imgui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/ImGuizmo.h"
#include <d3d12.h>
#include <dxgi1_4.h>

namespace vzimgui
{
	using namespace vzm;

	void UpdateTreeNode(const VID vid, const VID vidSelected, const std::function<void(const VID)>& callback);

	class VzImGuiFontManager
	{
	private:
		ImFont* customfont;
		ImFont* customfontlarge;
		ImFont* defaultfont;
		ImFont* iconfont;
		float fontScale = 1.0;
	public:
		void AddFont(const char* fontpath);

		ImFont* GetCustomFont() const { return customfont; }
		ImFont* GetCustomLargeFont() const { return customfontlarge; }
		ImFont* GetDefaultFont() const { return defaultfont; }
		ImFont* GetIconFont() const { return iconfont; }
	};

	class VzImGuizmo
	{
	private:
		ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
		float camera_pos[3] = { 4.0, 6.0, -15 }, camera_ang[3] = { 22, -13, 0 };
		VID highlightVID = INVALID_VID;
		VID subsetVID = INVALID_VID;
		bool useSnap = false;
		float snap[3] = { 0.25f, 0.25f, 0.25f };
		float bounds[6] = { -1.5f, -1.5f, -1.5f, 1.5f, 1.5f, 1.5f };
		float boundsSnap[3] = { 0.1f, 0.1f, 0.1f };
		bool boundSizing = false;
		bool boundSizingSnap = false;
		const float identityMatrix[16] =
		{ 1.f, 0.f, 0.f, 0.f,
			0.f, 1.f, 0.f, 0.f,
			0.f, 0.f, 1.f, 0.f,
			0.f, 0.f, 0.f, 1.f };
		bool bIsUsingWidget = false;
		ImGuizmo::OPERATION mCurrentGizmoOperation = (ImGuizmo::TRANSLATE);
		ImGuizmo::MODE mCurrentGizmoMode = (ImGuizmo::WORLD); //LOCAL

	public:
		void SetHighlighedVID(const VID vid) { highlightVID = vid; }
		void ApplyGizmo(const VID camVID, const ImVec2 pos, const ImVec2 size, ImDrawList* drawList);
	};
}