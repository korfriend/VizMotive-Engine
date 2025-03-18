#include "vzImGuiHelpers.h"
#include "IconsMaterialDesign.h"

#include <windowsx.h>
#include <shellscalingapi.h>

#define DPIAWARE
#define INCLUDEICONFONT
#define USEBOUNDSIZING

namespace vzimgui
{
	void UpdateTreeNode(const VID vid, const VID vidSelected, const std::function<void(const VID)>& callback)
	{
		vzm::VzSceneComp* component = (vzm::VzSceneComp*)vzm::GetComponent(vid);
		if (component == nullptr)
			return;
		std::string comp_name = component->GetName();
		if (comp_name.empty())
		{
			comp_name = "-";
		}
		else
		{
			switch (component->GetType())
			{
			case vzm::COMPONENT_TYPE::ACTOR: comp_name = "[A] " + comp_name; break;
			case vzm::COMPONENT_TYPE::CAMERA: comp_name = "[C] " + comp_name; break;
			case vzm::COMPONENT_TYPE::SLICER: comp_name = "[S] " + comp_name; break;
			case vzm::COMPONENT_TYPE::LIGHT: comp_name = "[L] " + comp_name; break;
			default: assert(0);
			}
		}

		static std::set<VID> pickedParents;

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
		if (vidSelected == vid)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		if (component->GetChildren().size() == 0)
		{
			flags |= ImGuiTreeNodeFlags_Leaf;
		}
		if (pickedParents.find(vid) != pickedParents.end())
		{
			ImGui::SetNextItemOpen(true);
		}

		if (ImGui::TreeNodeEx((const void*)(uint64_t)vid, flags, "%s", comp_name.c_str())) {
			if (ImGui::IsItemClicked())
			{
				vzlog("Clicked! %s", comp_name.c_str());
			}
			if (component->GetType() == vzm::COMPONENT_TYPE::ACTOR)
			{
				ImGui::SameLine();
				vzm::VzActor* actor = (vzm::VzActor*)component;
				bool visible = actor->IsVisibleWith(0x1);
				if (ImGui::Checkbox(" ", &visible))
				{
					actor->SetVisibleLayer(visible, 0x1);
				}
			}
			std::vector<VID> children = component->GetChildren();
			for (auto child_vid : children) {
				UpdateTreeNode(child_vid, vidSelected, callback);
			}
			ImGui::TreePop();
		}
		else {
			if (ImGui::IsItemClicked())
			{
				//vzlog("Clicked2! %s", comp_name.c_str());
			}
		}
	}

	void VzImGuiFontManager::AddFont(const char* fontpath)
	{
		//PE: Add all lang.
		static const ImWchar Generic_ranges_everything[] =
		{
		   0x0020, 0xFFFF, // Everything test.
		   0,
		};
		static const ImWchar Generic_ranges_most_needed[] =
		{
			0x0020, 0x00FF, // Basic Latin + Latin Supplement
			0x0100, 0x017F,	//0100 — 017F  	Latin Extended-A
			0x0180, 0x024F,	//0180 — 024F  	Latin Extended-B
			0,
		};

		float FONTUPSCALE = 1.0; //Font upscaling.
		float FontSize = 15.0f * fontScale;
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();

		customfont = io.Fonts->AddFontFromFileTTF(fontpath, FontSize * FONTUPSCALE, NULL, &Generic_ranges_everything[0]); //Set as default font.
		if (customfont)
		{
#ifdef INCLUDEICONFONT
			ImFontConfig config;
			config.MergeMode = true;
			config.GlyphOffset = ImVec2(0, 3);
			//config.GlyphMinAdvanceX = FontSize * FONTUPSCALE; // Use if you want to make the icon monospaced
			static const ImWchar icon_ranges[] = { ICON_MIN_MD, ICON_MAX_16_MD, 0 };
			io.Fonts->AddFontFromFileTTF(FONT_ICON_FILE_NAME_MD, FontSize * FONTUPSCALE, &config, icon_ranges);
#endif

			io.FontGlobalScale = 1.0f / FONTUPSCALE;
			customfontlarge = io.Fonts->AddFontFromFileTTF(fontpath, 20 * fontScale * FONTUPSCALE, NULL, &Generic_ranges_most_needed[0]); //Set as default font.
			if (!customfontlarge)
			{
				customfontlarge = customfont;
			}

#ifdef INCLUDEICONFONT
			config.MergeMode = true;
			config.GlyphOffset = ImVec2(0, 2);
			//config.GlyphMinAdvanceX = 20 * font_scale * FONTUPSCALE; // Use if you want to make the icon monospaced
			io.Fonts->AddFontFromFileTTF(FONT_ICON_FILE_NAME_MD, FontSize * FONTUPSCALE, &config, icon_ranges);
#endif

		}
		else
		{
			customfont = io.Fonts->AddFontDefault();
			customfontlarge = io.Fonts->AddFontDefault();
		}

		defaultfont = io.Fonts->AddFontDefault();
	}

	void VzImGuizmo::ApplyGizmo(const VID camVID, const ImVec2 pos, const ImVec2 size, ImDrawList* drawList)
	{
		ImGuizmo::BeginFrame();

		VzCamera* camera = (VzCamera*)GetComponent(camVID);
		ImGuizmo::SetGizmoSizeClipSpace(0.5f);
		VzBaseActor* highlighted_actor = (VzBaseActor*)GetComponent(highlightVID);
		if (highlighted_actor && camera)
		{
			//PE: Need to disable physics while editing.
			// physics::SetSimulationEnabled(false);

			XMFLOAT4X4 world;
			float matrixTranslation[3], matrixRotation[3], matrixScale[3];
			highlighted_actor->GetLocalMatrix(__FC44 world, true);

			float fCamView[16];
			float fCamProj[16];
			camera->GetViewMatrix(__FC44 fCamView[0], true);
			camera->GetProjectionMatrix(__FC44 fCamProj[0], true);

			ImGuizmo::SetDrawlist(drawList);
			ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);
			ImGuizmo::SetOrthographic(false);

			//PE: TODO - if mesh has pivot at center, use object aabb.getCenter(); as start matrix obj_world._41,obj_world._42,obj_world._43.

			ImGuizmo::Manipulate(fCamView, fCamProj, mCurrentGizmoOperation, mCurrentGizmoMode, &world._11, NULL, useSnap ? &snap[0] : NULL, boundSizing ? bounds : NULL, boundSizingSnap ? boundsSnap : NULL);
			if (ImGuizmo::IsUsing())
			{
				//PE: Use matrix directly. (rotation_local is using quaternion from XMQuaternionRotationMatrix)
				highlighted_actor->SetMatrix(__FC44 world._11, true);
				highlighted_actor->UpdateMatrix();

				//PE: Decompose , can give some matrix->euler problems. just left here for reference.
				//float matrixTranslation[3], matrixRotation[3], matrixScale[3];
				//ImGuizmo::DecomposeMatrixToComponents(&obj_world._11, matrixTranslation, matrixRotation, matrixScale);
				//obj_tranform->ClearTransform();
				//obj_tranform->Translate((const XMFLOAT3&)matrixTranslation[0]);
				//obj_tranform->RotateRollPitchYaw((const XMFLOAT3&)matrixRotation[0]);
				//obj_tranform->Scale((const XMFLOAT3&)matrixScale[0]);
				//obj_tranform->UpdateTransform();

				bIsUsingWidget = true;
			}
		}
	}
}