#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzBaseActor : VzSceneComp
	{
		VzBaseActor(const VID vid, const std::string& originFrom, const std::string& typeName, const SCENE_COMPONENT_TYPE scenecompType)
			: VzSceneComp(vid, originFrom, typeName, scenecompType) {}

		void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits);
		void SetPriority(const uint8_t priority);
	};

	struct API_EXPORT VzActor : VzBaseActor
	{
		VzActor(const VID vid, const std::string& originFrom)
			: VzBaseActor(vid, originFrom, "VzActor", SCENE_COMPONENT_TYPE::ACTOR) {}

		void SetRenderableRes(const VID vidGeo, const std::vector<VID>& vidMIs);
		void SetMI(const VID vidMI, const int slot = 0);

		void SetCastShadows(const bool enabled);
		void SetReceiveShadows(const bool enabled);
		void SetScreenSpaceContactShadows(const bool enabled);

		std::vector<VID> GetMIs();
		VID GetMI(const int slot = 0);
		VID GetMaterial(const int slot = 0);
		VID GetGeometry();

		void SetMorphWeights(const float* weights, const int count);
		int GetMorphTargetCount();
	};
}
