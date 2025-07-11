#include "vzImGuiHelpers.h"
#include "IconsMaterialDesign.h"

#include <windowsx.h>
#include <shellscalingapi.h>

namespace vzimgui
{
	void UpdateTreeNode(const VID vid, const VID vidSelected, const std::function<void(const VID)>& callback)
	{
		vzm::VzSceneObject* component = (vzm::VzSceneObject*)vzm::GetComponent(vid);
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
			case vzm::COMPONENT_TYPE::ACTOR_NODE: comp_name = "[Group] " + comp_name; break;
			case vzm::COMPONENT_TYPE::ACTOR_STATIC_MESH: comp_name = "[A-SM] " + comp_name; break;
			case vzm::COMPONENT_TYPE::ACTOR_VOLUME: comp_name = "[A-V] " + comp_name; break;
			case vzm::COMPONENT_TYPE::ACTOR_GSPLAT: comp_name = "[A-G] " + comp_name; break;
			case vzm::COMPONENT_TYPE::ACTOR_SPRITE: comp_name = "[A-SP] " + comp_name; break;
			case vzm::COMPONENT_TYPE::ACTOR_SPRITEFONT: comp_name = "[A-SPF] " + comp_name; break;
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

		std::vector<VID> children = component->GetChildren();
		if (children.size() == 0)
		{
			flags |= ImGuiTreeNodeFlags_Leaf;
		}

		if (pickedParents.find(vid) != pickedParents.end())
		{
			ImGui::SetNextItemOpen(true);
		}

		bool hasVisibilityControl = false;
		switch (component->GetType())
		{
		case vzm::COMPONENT_TYPE::ACTOR_NODE:
		case vzm::COMPONENT_TYPE::ACTOR_STATIC_MESH:
		case vzm::COMPONENT_TYPE::ACTOR_VOLUME:
		case vzm::COMPONENT_TYPE::ACTOR_GSPLAT:
		case vzm::COMPONENT_TYPE::ACTOR_SPRITE:
		case vzm::COMPONENT_TYPE::ACTOR_SPRITEFONT:
		{
			vzm::VzActor* actor = (vzm::VzActor*)component;
			bool visible = actor->IsVisibleWith(0x1);
			if (ImGui::Checkbox(("##vis_" + std::to_string(vid)).c_str(), &visible))
			{
				actor->SetVisibleLayer(visible, 0x1, true);
			}
			hasVisibilityControl = true;
		}
		break;
		case vzm::COMPONENT_TYPE::LIGHT:
		{
			vzm::VzLight* light = (vzm::VzLight*)component;
			bool visible = light->IsVisibleWith(0x1);
			if (ImGui::Checkbox(("##vis_" + std::to_string(vid)).c_str(), &visible))
			{
				light->SetVisibleLayer(visible, 0x1, true);
			}
			hasVisibilityControl = true;
		}
		break;
		}

		if (hasVisibilityControl)
		{
			ImGui::SameLine(); 
		}

		if (ImGui::TreeNodeEx((const void*)(uint64_t)vid, flags, "%s", comp_name.c_str()))
		{
			if (ImGui::IsItemClicked() && callback)
			{
				callback(vid);
			}

			for (auto child_vid : children)
			{
				UpdateTreeNode(child_vid, vidSelected, callback);
			}
			ImGui::TreePop();
		}
		else
		{
			if (ImGui::IsItemClicked() && callback)
			{
				callback(vid);
			}
		}
	}

	ImFont* customfont;
	ImFont* customfontlarge;
	ImFont* defaultfont;
	ImFont* iconfont;

	void IGTextTitle(const char* text)
	{
		ImGuiWindow* window = ImGui::GetCurrentWindowRead();
		float w = ImGui::GetContentRegionAvail().x;
		if (customfontlarge) ImGui::PushFont(customfontlarge);
		float textwidth = ImGui::CalcTextSize(text).x;
		ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2((w * 0.5f) - (textwidth * 0.5f) - (window->DC.Indent.x * 0.5f), 0.0f));
		ImGui::Text("%s", text);
		if (customfontlarge) ImGui::PopFont();
	}
	
	std::unordered_map<VzTexture::TextureFormat, std::string> TextureFormatToString = {
	{ VzTexture::TextureFormat::UNKNOWN, "UNKNOWN" },
	{ VzTexture::TextureFormat::R32G32B32A32_FLOAT, "R32G32B32A32_FLOAT" },
	{ VzTexture::TextureFormat::R32G32B32A32_UINT, "R32G32B32A32_UINT" },
	{ VzTexture::TextureFormat::R32G32B32A32_SINT, "R32G32B32A32_SINT" },
	{ VzTexture::TextureFormat::R32G32B32_FLOAT, "R32G32B32_FLOAT" },
	{ VzTexture::TextureFormat::R32G32B32_UINT, "R32G32B32_UINT" },
	{ VzTexture::TextureFormat::R32G32B32_SINT, "R32G32B32_SINT" },
	{ VzTexture::TextureFormat::R16G16B16A16_FLOAT, "R16G16B16A16_FLOAT" },
	{ VzTexture::TextureFormat::R16G16B16A16_UNORM, "R16G16B16A16_UNORM" },
	{ VzTexture::TextureFormat::R16G16B16A16_UINT, "R16G16B16A16_UINT" },
	{ VzTexture::TextureFormat::R16G16B16A16_SNORM, "R16G16B16A16_SNORM" },
	{ VzTexture::TextureFormat::R16G16B16A16_SINT, "R16G16B16A16_SINT" },
	{ VzTexture::TextureFormat::R32G32_FLOAT, "R32G32_FLOAT" },
	{ VzTexture::TextureFormat::R32G32_UINT, "R32G32_UINT" },
	{ VzTexture::TextureFormat::R32G32_SINT, "R32G32_SINT" },
	{ VzTexture::TextureFormat::D32_FLOAT_S8X24_UINT, "D32_FLOAT_S8X24_UINT" },
	{ VzTexture::TextureFormat::R10G10B10A2_UNORM, "R10G10B10A2_UNORM" },
	{ VzTexture::TextureFormat::R10G10B10A2_UINT, "R10G10B10A2_UINT" },
	{ VzTexture::TextureFormat::R11G11B10_FLOAT, "R11G11B10_FLOAT" },
	{ VzTexture::TextureFormat::R8G8B8A8_UNORM, "R8G8B8A8_UNORM" },
	{ VzTexture::TextureFormat::R8G8B8A8_UNORM_SRGB, "R8G8B8A8_UNORM_SRGB" },
	{ VzTexture::TextureFormat::R8G8B8A8_UINT, "R8G8B8A8_UINT" },
	{ VzTexture::TextureFormat::R8G8B8A8_SNORM, "R8G8B8A8_SNORM" },
	{ VzTexture::TextureFormat::R8G8B8A8_SINT, "R8G8B8A8_SINT" },
	{ VzTexture::TextureFormat::B8G8R8A8_UNORM, "B8G8R8A8_UNORM" },
	{ VzTexture::TextureFormat::B8G8R8A8_UNORM_SRGB, "B8G8R8A8_UNORM_SRGB" },
	{ VzTexture::TextureFormat::R16G16_FLOAT, "R16G16_FLOAT" },
	{ VzTexture::TextureFormat::R16G16_UNORM, "R16G16_UNORM" },
	{ VzTexture::TextureFormat::R16G16_UINT, "R16G16_UINT" },
	{ VzTexture::TextureFormat::R16G16_SNORM, "R16G16_SNORM" },
	{ VzTexture::TextureFormat::R16G16_SINT, "R16G16_SINT" },
	{ VzTexture::TextureFormat::D32_FLOAT, "D32_FLOAT" },
	{ VzTexture::TextureFormat::R32_FLOAT, "R32_FLOAT" },
	{ VzTexture::TextureFormat::R32_UINT, "R32_UINT" },
	{ VzTexture::TextureFormat::R32_SINT, "R32_SINT" },
	{ VzTexture::TextureFormat::D24_UNORM_S8_UINT, "D24_UNORM_S8_UINT" },
	{ VzTexture::TextureFormat::R9G9B9E5_SHAREDEXP, "R9G9B9E5_SHAREDEXP" },
	{ VzTexture::TextureFormat::R8G8_UNORM, "R8G8_UNORM" },
	{ VzTexture::TextureFormat::R8G8_UINT, "R8G8_UINT" },
	{ VzTexture::TextureFormat::R8G8_SNORM, "R8G8_SNORM" },
	{ VzTexture::TextureFormat::R8G8_SINT, "R8G8_SINT" },
	{ VzTexture::TextureFormat::R16_FLOAT, "R16_FLOAT" },
	{ VzTexture::TextureFormat::D16_UNORM, "D16_UNORM" },
	{ VzTexture::TextureFormat::R16_UNORM, "R16_UNORM" },
	{ VzTexture::TextureFormat::R16_UINT, "R16_UINT" },
	{ VzTexture::TextureFormat::R16_SNORM, "R16_SNORM" },
	{ VzTexture::TextureFormat::R16_SINT, "R16_SINT" },
	{ VzTexture::TextureFormat::R8_UNORM, "R8_UNORM" },
	{ VzTexture::TextureFormat::R8_UINT, "R8_UINT" },
	{ VzTexture::TextureFormat::R8_SNORM, "R8_SNORM" },
	{ VzTexture::TextureFormat::R8_SINT, "R8_SINT" },
	{ VzTexture::TextureFormat::BC1_UNORM, "BC1_UNORM" },
	{ VzTexture::TextureFormat::BC1_UNORM_SRGB, "BC1_UNORM_SRGB" },
	{ VzTexture::TextureFormat::BC2_UNORM, "BC2_UNORM" },
	{ VzTexture::TextureFormat::BC2_UNORM_SRGB, "BC2_UNORM_SRGB" },
	{ VzTexture::TextureFormat::BC3_UNORM, "BC3_UNORM" },
	{ VzTexture::TextureFormat::BC3_UNORM_SRGB, "BC3_UNORM_SRGB" },
	{ VzTexture::TextureFormat::BC4_UNORM, "BC4_UNORM" },
	{ VzTexture::TextureFormat::BC4_SNORM, "BC4_SNORM" },
	{ VzTexture::TextureFormat::BC5_UNORM, "BC5_UNORM" },
	{ VzTexture::TextureFormat::BC5_SNORM, "BC5_SNORM" },
	{ VzTexture::TextureFormat::BC6H_UF16, "BC6H_UF16" },
	{ VzTexture::TextureFormat::BC6H_SF16, "BC6H_SF16" },
	{ VzTexture::TextureFormat::BC7_UNORM, "BC7_UNORM" },
	{ VzTexture::TextureFormat::BC7_UNORM_SRGB, "BC7_UNORM_SRGB" },
	{ VzTexture::TextureFormat::NV12, "NV12" },
	};

	std::unordered_map<ShaderType, std::string> ShaderTypeToString = {
	{ ShaderType::PHONG, "PHONG" },
	{ ShaderType::PBR, "PBR" },
	{ ShaderType::UNLIT, "UNLIT" },
	{ ShaderType::VOLUMEMAP, "VOLUMEMAP" },
	};

	void UpdateResourceMonitor(const std::function<void(const VID)>& callback)
	{
		std::vector<VzBaseComp*> actors;
		std::vector<VzBaseComp*> geometries;
		std::vector<VzBaseComp*> materials;
		std::vector<VzBaseComp*> textures;
		std::vector<VzBaseComp*> volumes;
		std::vector<VzBaseComp*> cameras;
		std::vector<VzBaseComp*> slicers;
		std::vector<VzBaseComp*> renderers;
		std::vector<VzBaseComp*> scenes;
		std::vector<VzBaseComp*> archives;

		size_t total_user_components = 0;
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::ACTOR_STATIC_MESH, actors);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::ACTOR_VOLUME, actors);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::ACTOR_GSPLAT, actors);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::ACTOR_SPRITE, actors);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::ACTOR_SPRITEFONT, actors);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::GEOMETRY, geometries);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::MATERIAL, materials);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::TEXTURE, textures);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::VOLUME, volumes);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::CAMERA, cameras);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::SLICER, slicers);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::RENDERER, renderers);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::SCENE, scenes);
		total_user_components += vzm::GetComponentsByType(COMPONENT_TYPE::ARCHIVE, archives);
		
		IGTextTitle("Resource Monitor");

		//ImGuiWindow* window = ImGui::GetCurrentWindowRead();
		if (customfontlarge) ImGui::PushFont(customfontlarge); 
		
		ImGui::Text("** Total User Components: %d", (uint32_t)total_user_components);

		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Bullet;
		if (ImGui::TreeNodeEx("GEOMETRY", flags, "COMPONENT_TYPE::GEOMETRY")) {
			for (auto it : geometries)
			{
				// TODO: GET_RESOURCE_SIZE
				VID vid = it->GetVID();
				std::string name = it->GetName();
				uint32_t num_parts = (uint32_t)((VzGeometry*)it)->GetNumParts();
				const char* label = name.c_str();
				if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_Leaf, num_parts > 1 ? "%s(%d): %d Parts, %f MB" : "%s(%d): %d Part, %f MB"
					, label, vid, num_parts, (float)((VzGeometry*)it)->GetMemoryUsageCPU() / 1024.f / 1024.f)) {
					if (ImGui::IsItemClicked())
					{
						vzlog("Clicked! %s", label);
						callback(vid);
					}
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("TEXTURE", flags, "COMPONENT_TYPE::TEXTURE")) {
			for (auto it : textures)
			{
				VID vid = it->GetVID();
				std::string name = it->GetName();
				const char* label = name.c_str();
				uint32_t w, h, d;
				((VzTexture*)it)->GetTextureSize(&w, &h, &d);
				std::string format_str = TextureFormatToString[((VzTexture*)it)->GetTextureFormat()];
				if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_Leaf,
					"%s(%d): %dx%dx%d (%s)", label, vid, w, h, d, format_str.c_str())) {
					if (ImGui::IsItemClicked())
					{
						vzlog("Clicked! %s", label);
					}
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("VOLUME", flags, "COMPONENT_TYPE::VOLUME"))
		{
			for (auto it : volumes)
			{
				VID vid = it->GetVID();
				std::string name = it->GetName();
				const char* label = name.c_str();
				uint32_t w, h, d;
				((VzTexture*)it)->GetTextureSize(&w, &h, &d);
				std::string format_str = TextureFormatToString[((VzTexture*)it)->GetTextureFormat()];
				if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_Leaf,
					"%s(%d): %d x %d x %d (%s)", label, vid, w, h, d, format_str.c_str())) {
					if (ImGui::IsItemClicked())
					{
						vzlog("Clicked! %s", label);
					}
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("RENDERER", flags, "COMPONENT_TYPE::RENDERER")) {
			for (auto it : renderers)
			{
				VID vid = it->GetVID();
				std::string name = it->GetName();
				const char* label = name.c_str();
				uint32_t w, h;
				((VzRenderer*)it)->GetCanvas(&w, &h, nullptr, nullptr);
				if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_Leaf,
					"%s(%d): %d x %d", label, vid, w, h)) {
					if (ImGui::IsItemClicked())
					{
						vzlog("Clicked! %s", label);
					}
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("MATERIAL", flags, "COMPONENT_TYPE::MATERIAL")) {
			for (auto it : materials)
			{
				VID vid = it->GetVID();
				std::string name = it->GetName();
				const char* label = name.c_str();
				std::string shader_type_str =  ShaderTypeToString[((VzMaterial*)it)->GetShaderType()];
				if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_Leaf,
					"%s(%d): %s", label, vid, shader_type_str.c_str())) {
					if (ImGui::IsItemClicked())
					{
						vzlog("Clicked! %s", label);
					}
					ImGui::TreePop();
				}
			}
			ImGui::TreePop();
		}

		if (customfontlarge) ImGui::PopFont();
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

		{
			vzimgui::customfont = this->customfont;;
			vzimgui::customfontlarge = this->customfontlarge;
			vzimgui::defaultfont = this->defaultfont;
			vzimgui::iconfont = this->iconfont;
		}
	}

	void VzImGuizmo::ApplyGizmo(const VID camVID, const ImVec2 pos, const ImVec2 size, ImDrawList* drawList)
	{
		ImGuizmo::BeginFrame();

		VzCamera* camera = (VzCamera*)GetComponent(camVID);
		ImGuizmo::SetGizmoSizeClipSpace(0.5f);
		VzActor* highlighted_actor = (VzActor*)GetComponent(highlightVID);
		if (highlighted_actor && camera)
		{
			//PE: Need to disable physics while editing.
			// physics::SetSimulationEnabled(false);

			XMFLOAT4X4 world;
			highlighted_actor->GetLocalMatrix(__FC44 world, true);

			float fCamView[16];
			float fCamProj[16];
			camera->GetViewMatrix(__FC44 fCamView[0], true);
			camera->GetProjectionMatrix(__FC44 fCamProj[0], true);

			ImGuizmo::SetDrawlist(drawList);
			ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);
			ImGuizmo::SetOrthographic(false);

			//PE: TODO - if mesh has pivot at center, use object aabb.getCenter(); as start matrix obj_world._41,obj_world._42,obj_world._43.

			ImGuizmo::Manipulate(fCamView, fCamProj, currentGizmoOperation, currentGizmoMode, &world._11, NULL, useSnap ? &snap[0] : NULL, boundSizing ? bounds : NULL, boundSizingSnap ? boundsSnap : NULL);
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