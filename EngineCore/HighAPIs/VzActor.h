#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzBaseActor : VzSceneComp
	{
		VzBaseActor(const VID vid, const std::string& originFrom, const COMPONENT_TYPE scenecompType)
			: VzSceneComp(vid, originFrom, scenecompType) {}

		void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits);
	};

	struct API_EXPORT VzActor : VzBaseActor
	{
		VzActor(const VID vid, const std::string& originFrom)
			: VzBaseActor(vid, originFrom, COMPONENT_TYPE::ACTOR) {}

		bool IsRenderable() const;

		void SetGeometry(const GeometryVID vid);
		void SetGeometry(const VzBaseComp* geometry) { SetGeometry(geometry->GetVID()); }
		void SetMaterial(const MaterialVID vid, const int slot = 0);
		void SetMaterial(const VzBaseComp* material, const int slot = 0) { SetMaterial(material->GetVID(), slot); }

		void SetMaterials(const std::vector<MaterialVID> vids);

		void SetCastShadows(const bool enabled);
		void SetReceiveShadows(const bool enabled);

		void EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled);
		void SetClipPlane(const vfloat4& clipPlane);
		void SetClipBox(const vfloat4x4& clipBox);

		std::vector<MaterialVID> GetMaterials() const;
		MaterialVID GetMaterial(const int slot = 0) const;
		GeometryVID GetGeometry() const;
	};
}
