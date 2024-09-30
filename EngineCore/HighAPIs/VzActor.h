#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzBaseActor : VzSceneComp
	{
		VzBaseActor(const VID vid, const std::string& originFrom, const std::string& typeName, const SCENE_COMPONENT_TYPE scenecompType)
			: VzSceneComp(vid, originFrom, typeName, scenecompType) {}

		void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits);
	};

	struct API_EXPORT VzActor : VzBaseActor
	{
		VzActor(const VID vid, const std::string& originFrom)
			: VzBaseActor(vid, originFrom, "VzActor", SCENE_COMPONENT_TYPE::ACTOR) {}

		bool IsRenderable();

		void SetGeometry(const GeometryVID vid);
		void SetMaterial(const MaterialVID vid, const int slot = 0);

		void SetMaterials(const std::vector<MaterialVID> vids);

		void SetCastShadows(const bool enabled);
		void SetReceiveShadows(const bool enabled);

		std::vector<MaterialVID> GetMaterials();
		MaterialVID GetMaterial(const int slot = 0);
		GeometryVID GetGeometry();
	};
}
