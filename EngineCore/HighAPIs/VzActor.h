#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzActor : VzSceneObject
	{
		VzActor(const VID vid, const std::string& originFrom, const COMPONENT_TYPE scenecompType)
			: VzSceneObject(vid, originFrom, scenecompType) {}
		virtual ~VzActor() = default;

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
			: VzActor(vid, originFrom, COMPONENT_TYPE::ACTOR_STATIC_MESH) {
		}
		virtual ~VzActorStaticMesh() = default;

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
		VzActorSprite(const VID vid, const std::string& originFrom) : VzActor(vid, originFrom, COMPONENT_TYPE::ACTOR_SPRITE) { }
		virtual ~VzActorSprite() = default;

		virtual void SetSpriteTexture(const VID vidTexture);

		void EnableHidden(bool enabled = true);
		void EnableUpdate(bool enabled = true);
		void EnableCameraFacing(bool enabled = true);
		void EnableCameraScaling(bool enabled = true);

		void EnableExtractNormalMap(bool enabled = true);
		void EnableMirror(bool enabled = true);
		void EnableHDR10OutputMapping(bool enabled = true);
		void EnableLinearOutputMapping(bool enabled = true);
		void EnableCornerRounding(bool enabled = true);
		void EnableDepthTest(bool enabled = true);
		void EnableHighlight(bool enabled = true);

		bool IsDisableUpdate() const;
		bool IsCameraFacing() const;
		bool IsCameraScaling() const;
		bool IsHidden() const;
		bool IsExtractNormalMapEnabled() const;
		bool IsMirrorEnabled() const;
		bool IsHDR10OutputMappingEnabled() const;
		bool IsLinearOutputMappingEnabled() const;
		bool IsCornerRoundingEnabled() const;
		bool IsDepthTestEnabled() const;
		bool IsHighlightEnabled() const;

		void SetSpritePosition(const vfloat3& p);
		void SetSpriteScale(const vfloat2& s);
		void SetSpriteUVOffset(const vfloat2& uvOffset);
		void SetSpriteRotation(const float v);
		void SetSpriteOpacity(const float v);
		void SetSpriteFade(const float v);
	};

	struct API_EXPORT VzActorSpriteFont : VzActor
	{
		enum Alignment : uint32_t
		{
			FONTALIGN_LEFT,		// left alignment (horizontal)
			FONTALIGN_CENTER,	// center alignment (horizontal or vertical)
			FONTALIGN_RIGHT,	// right alignment (horizontal)
			FONTALIGN_TOP,		// top alignment (vertical)
			FONTALIGN_BOTTOM	// bottom alignment (vertical)
		};

		VzActorSpriteFont(const VID vid, const std::string& originFrom) : VzActor(vid, originFrom, COMPONENT_TYPE::ACTOR_SPRITEFONT) {}
		virtual ~VzActorSpriteFont() = default;
		
		void SetText(const std::string& text);

		void EnableHidden(bool enabled = true);
		void EnableUpdate(bool enabled = true);
		void EnableCameraFacing(bool enabled = true);
		void EnableCameraScaling(bool enabled = true);

		void EnableSDFRendering(bool enabled = true);
		void EnableHDR10OutputMapping(bool enabled = true);
		void EnableLinearOutputMapping(bool enabled = true);
		void EnableDepthTest(bool enabled = true);
		void EnableFlipHorizontally(bool enabled = true);
		void EnableFlipVertically(bool enabled = true);

		bool IsDisableUpdate() const;
		bool IsCameraFacing() const;
		bool IsCameraScaling() const;
		bool IsHidden() const;
		bool IsSDFRendering() const;
		bool IsHDR10OutputMapping() const;
		bool IsLinearOutputMapping() const;
		bool IsDepthTest() const;
		bool IsFlipHorizontally() const;
		bool IsFlipVertically() const;

		void SetFontStyle(const std::string& fontStyle);
		void SetFontSize(const int size);
		void SetSpacing(const vfloat2& spacing);
		void SetHorizonAlign(const Alignment horizonAlign);
		void SetVerticalAlign(const Alignment verticalAlign);
		void SetColor(const vfloat4& color);
		void SetShadowColor(const vfloat4& shadowColor);
		void SetWrap(const float wrap);
		void SetSoftness(const float softness);
		void SetBolden(const float bolden);
		void SetShadowSoftness(const float shadowSoftness);
		void SetShadowBolden(const float shadowBolden);
		void SetShadowOffset(const vfloat2 shadowOffset);
		void SetHdrScale(const float hdrScaling);
		void SetIntensity(const float intensity);
		void SetShadowIntensity(const float shadowIntensity);
	};

	struct API_EXPORT VzGSplatActor : VzActor
	{
	};

	struct API_EXPORT VzParticleActor : VzActor
	{
	};
}
