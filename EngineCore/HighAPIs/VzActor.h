#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzActor : VzSceneObject
	{
		VzActor(const VID vid, const std::string& originFrom, const COMPONENT_TYPE scenecompType)
			: VzSceneObject(vid, originFrom, scenecompType) {}

		void SetVisibleLayerMask(const uint32_t visibleLayerMask, const bool includeDescendants = false);
		void SetVisibleLayer(const bool visible, const uint32_t layerBits, const bool includeDescendants = false);
		uint32_t GetVisibleLayerMask() const;
		bool IsVisibleWith(const uint32_t layerBits) const;

		bool IsRenderable() const;

		void EnablePickable(const bool enabled, const bool includeDescendants = false);
		bool IsPickable() const;
	};

	struct API_EXPORT VzActorStaticMesh : VzActor
	{
		VzActorStaticMesh(const VID vid, const std::string& originFrom)
			: VzActor(vid, originFrom, COMPONENT_TYPE::ACTOR_STATIC_MESH) {}

		void SetGeometry(const GeometryVID vid);
		void SetGeometry(const VzBaseComp* geometry) { SetGeometry(geometry->GetVID()); }
		void SetMaterial(const MaterialVID vid, const int slot = 0);
		void SetMaterial(const VzBaseComp* material, const int slot = 0) { SetMaterial(material->GetVID(), slot); }

		void SetMaterials(const std::vector<MaterialVID> vids);

		void EnableCastShadows(const bool enabled);
		void EnableReceiveShadows(const bool enabled);
		void EnableSlicerSolidFill(const bool enabled);
		void EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled);
		void EnableOutline(const bool enabled);
		void EnableUndercut(const bool enabled);

		void SetClipPlane(const vfloat4& clipPlane);
		void SetClipBox(const vfloat4x4& clipBox);
		bool IsClipperEnabled(bool* clipBoxEnabled = nullptr, bool* clipPlaneEnabled = nullptr) const;

		void SetOutineThickness(const float v);
		void SetOutineColor(const vfloat3 v);
		void SetOutineThreshold(const float v);
		void SetUndercutDirection(const vfloat3 v);
		void SetUndercutColor(const vfloat3 v);

		bool CollisionCheck(const ActorVID targetActorVID, int* partIndexSrc = nullptr, int* partIndexTarget = nullptr, int* triIndexSrc = nullptr, int* triIndexTarget = nullptr) const;

		void DebugRender(const std::string& debugScript);

		std::vector<MaterialVID> GetMaterials() const;
		MaterialVID GetMaterial(const int slot = 0) const;
		GeometryVID GetGeometry() const;

		// ----- collider setting ----
		void AssignCollider();
		bool HasCollider() const;
		bool ColliderCollisionCheck(const ActorVID targetActorVID) const;
	};

	struct API_EXPORT VzVolumeActor : VzActor
	{
		// TODO
		// No need for GeometryComponent
		// 1. RenderableVolumeComponent 
		// 2. Scene handles this renderable separately.. (see SpriteComponent)
		// 3. Rename existing VzActor to VzStaticMeshActor : VzMeshActor (in the future,  VzSkeletalMeshActor : VzMeshActor)
	};

	struct API_EXPORT VzActorSprite : VzActor
	{
		VzActorSprite(const VID vid, const std::string& originFrom)
			: VzActor(vid, originFrom, COMPONENT_TYPE::ACTOR_SPRITE) {
		}
	};

	struct API_EXPORT VzActorSpriteFont : VzActorSprite
	{
		VzActorSpriteFont(const VID vid, const std::string& originFrom)
			: VzActorSprite(vid, originFrom) {
			type_ = COMPONENT_TYPE::ACTOR_SPRITEFONT;
		}
	};

	struct API_EXPORT VzGSplatActor : VzActor
	{
	};

	struct API_EXPORT VzParticleActor : VzActor
	{
	};
}
