#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	// Interfaces
	struct API_EXPORT VIzClip
	{
		virtual void EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled) = 0;
		virtual void SetClipPlane(const vfloat4& clipPlane) = 0;
		virtual void SetClipBox(const vfloat4x4& clipBox) = 0;
		virtual bool IsClipperEnabled(bool* clipBoxEnabled = nullptr, bool* clipPlaneEnabled = nullptr) const = 0;
	};
	struct API_EXPORT VIzHighlight
	{
		virtual void EnableOutline(const bool enabled) = 0;
		virtual void SetOutineThickness(const float v) = 0;
		virtual void SetOutineColor(const vfloat3 v) = 0;
		virtual void SetOutineThreshold(const float v) = 0;
	};
	struct API_EXPORT VIzCollision
	{
		virtual void AssignCollider() = 0;
		virtual bool HasCollider() const = 0;
		virtual bool ColliderCollisionCheck(const ActorVID targetActorVID) const = 0;
		virtual bool CollisionCheck(const ActorVID targetActorVID,
			int* partIndexSrc = nullptr, int* partIndexTarget = nullptr, int* triIndexSrc = nullptr, int* triIndexTarget = nullptr) const = 0;
	};
	struct API_EXPORT VIzGI
	{
		virtual void EnableCastShadows(const bool enabled) = 0;
		virtual void EnableReceiveShadows(const bool enabled) = 0;
	};
	struct API_EXPORT VIzSprite
	{
		virtual void EnableHidden(bool enabled = true) = 0;
		virtual void EnableUpdate(bool enabled = true) = 0;
		virtual void EnableCameraFacing(bool enabled = true) = 0;
		virtual void EnableCameraScaling(bool enabled = true) = 0;
		virtual void EnableHDR10OutputMapping(bool enabled = true) = 0;
		virtual void EnableLinearOutputMapping(bool enabled = true) = 0;
		virtual void EnableDepthTest(bool enabled = true) = 0;

		virtual bool IsHidden() const = 0;
		virtual bool IsDisableUpdate() const = 0;
		virtual bool IsCameraFacing() const = 0;
		virtual bool IsCameraScaling() const = 0;
		virtual bool IsHDR10OutputMappingEnabled() const = 0;
		virtual bool IsLinearOutputMappingEnabled() const = 0;
		virtual bool IsDepthTestEnabled() const = 0;
	};
}
namespace vzm
{
	// This is base Actor class
	struct API_EXPORT VzActor : VzSceneObject
	{
		VzActor(const VID vid, const std::string& originFrom, const COMPONENT_TYPE scenecompType)
			: VzSceneObject(vid, originFrom, scenecompType) {}
		virtual ~VzActor() = default;

		bool IsRenderable() const;

		void EnablePickable(const bool enabled, const bool includeDescendants = true);
		bool IsPickable() const;

		void EnableUnlit(const bool enabled, const bool includeDescendants = true);

		std::vector<MaterialVID> GetAllMaterials(const bool includeDescendants = true);
	};

	struct API_EXPORT VzActorStaticMesh : VzActor, VIzClip, VIzHighlight, VIzCollision, VIzGI
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
		std::vector<MaterialVID> GetMaterials() const;
		MaterialVID GetMaterial(const int slot = 0) const;
		GeometryVID GetGeometry() const;
		void EnableSlicerSolidFill(const bool enabled);	// only available in Slicer

		// VzActorUndercutMesh ??
		void SetUndercutDirection(const vfloat3 v);
		void SetUndercutColor(const vfloat3 v);
		void EnableUndercut(const bool enabled);

		// ----- interfaces for VIzClip -----
		void EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled) override;
		void SetClipPlane(const vfloat4& clipPlane) override;
		void SetClipBox(const vfloat4x4& clipBox) override;
		bool IsClipperEnabled(bool* clipBoxEnabled = nullptr, bool* clipPlaneEnabled = nullptr) const override;

		// ----- interfaces for VIzHighlight -----
		void EnableOutline(const bool enabled) override;
		void SetOutineThickness(const float v) override;
		void SetOutineColor(const vfloat3 v) override;
		void SetOutineThreshold(const float v) override;

		// ----- interfaces for VIzCollision -----		
		void AssignCollider() override;
		bool HasCollider() const override;
		bool ColliderCollisionCheck(const ActorVID targetActorVID) const override;
		bool CollisionCheck(const ActorVID targetActorVID, 
			int* partIndexSrc = nullptr, int* partIndexTarget = nullptr, int* triIndexSrc = nullptr, int* triIndexTarget = nullptr) const override;

		// ----- interfaces for VIzGI -----
		void EnableCastShadows(const bool enabled) override;
		void EnableReceiveShadows(const bool enabled) override;
	};

	struct API_EXPORT VzActorVolume : VzActor
	{
		VzActorVolume(const VID vid, const std::string& originFrom)
			: VzActor(vid, originFrom, COMPONENT_TYPE::ACTOR_VOLUME) {
		}
		virtual ~VzActorVolume() = default;
		
		void SetMaterial(const MaterialVID vid);
		void SetMaterial(const VzBaseComp* material) { SetMaterial(material->GetVID()); }

		MaterialVID CreateAndSetMaterial(const VolumeVID vidVolume,
			const VolumeVID vidVolumeSemantic, const VolumeVID vidVolumeSculpt, const TextureVID vidOTF, const TextureVID vidWindowing);
		MaterialVID GetMaterial() const;
	};

	struct API_EXPORT VzActorSprite : VzActor, VIzSprite
	{
		VzActorSprite(const VID vid, const std::string& originFrom) : VzActor(vid, originFrom, COMPONENT_TYPE::ACTOR_SPRITE) { }
		virtual ~VzActorSprite() = default;

		virtual void SetSpriteTexture(const VID vidTexture);

		void EnableExtractNormalMap(bool enabled = true);
		void EnableMirror(bool enabled = true);
		void EnableCornerRounding(bool enabled = true);
		void EnableHighlight(bool enabled = true);

		bool IsExtractNormalMapEnabled() const;
		bool IsMirrorEnabled() const;
		bool IsCornerRoundingEnabled() const;
		bool IsHighlightEnabled() const;

		void SetSpriteScale(const vfloat2& s);
		void SetSpritePosition(const vfloat3& p);
		void SetSpriteUVOffset(const vfloat2& uvOffset);
		void SetSpriteRotation(const float v);
		void SetSpriteOpacity(const float v);
		void SetSpriteFade(const float v);

		// ----- interfaces for VIzSprite -----
		void EnableHidden(bool enabled = true) override;
		void EnableUpdate(bool enabled = true) override;
		void EnableCameraFacing(bool enabled = true) override;
		void EnableCameraScaling(bool enabled = true) override;
		void EnableHDR10OutputMapping(bool enabled = true) override;
		void EnableLinearOutputMapping(bool enabled = true) override;
		void EnableDepthTest(bool enabled = true) override;

		bool IsHidden() const override;
		bool IsDisableUpdate() const override;
		bool IsCameraFacing() const override;
		bool IsCameraScaling() const override;
		bool IsHDR10OutputMappingEnabled() const override;
		bool IsLinearOutputMappingEnabled() const override;
		bool IsDepthTestEnabled() const override;
	};

	struct API_EXPORT VzActorSpriteFont : VzActor, VIzSprite
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

		void EnableSDFRendering(bool enabled = true);
		void EnableFlipHorizontally(bool enabled = true);
		void EnableFlipVertically(bool enabled = true);

		bool IsSDFRendering() const;
		bool IsFlipHorizontally() const;
		bool IsFlipVertically() const;

		void SetFontStyle(const std::string& fontStyle);
		void SetFontSize(const int size);  // ATLAS Line Height
		void SetFontScale(const float scale);
		void SetFontSpacing(const vfloat2& spacing);
		void SetFontHorizonAlign(const Alignment horizonAlign);
		void SetFontVerticalAlign(const Alignment verticalAlign);
		void SetFontColor(const vfloat4& color);
		void SetFontShadowColor(const vfloat4& shadowColor);
		void SetFontWrap(const float wrap);
		void SetFontSoftness(const float softness);
		void SetFontBolden(const float bolden);
		void SetFontShadowSoftness(const float shadowSoftness);
		void SetShadowBolden(const float shadowBolden);
		void SetFontShadowOffset(const vfloat2 shadowOffset);
		void SetFontHdrScale(const float hdrScaling);
		void SetFontIntensity(const float intensity);
		void SetFontShadowIntensity(const float shadowIntensity);

		int GetFontSize() const;

		// ----- interfaces for VIzSprite -----
		void EnableHidden(bool enabled = true) override;
		void EnableUpdate(bool enabled = true) override;
		void EnableCameraFacing(bool enabled = true) override;
		void EnableCameraScaling(bool enabled = true) override;
		void EnableHDR10OutputMapping(bool enabled = true) override;
		void EnableLinearOutputMapping(bool enabled = true) override;
		void EnableDepthTest(bool enabled = true) override;

		bool IsHidden() const override;
		bool IsDisableUpdate() const override;
		bool IsCameraFacing() const override;
		bool IsCameraScaling() const override;
		bool IsHDR10OutputMappingEnabled() const override;
		bool IsLinearOutputMappingEnabled() const override;
		bool IsDepthTestEnabled() const override;
	};

	struct API_EXPORT VzActorGSplat : VzActor
	{
		VzActorGSplat(const VID vid, const std::string& originFrom)
			: VzActor(vid, originFrom, COMPONENT_TYPE::ACTOR_GSPLAT) {
		}
		virtual ~VzActorGSplat() = default;

		void SetGeometry(const GeometryVID vid);
		void SetGeometry(const VzBaseComp* geometry) { SetGeometry(geometry->GetVID()); }
		void SetMaterial(const MaterialVID vid);
		void SetMaterial(const VzBaseComp* material) { SetMaterial(material->GetVID()); }

		GeometryVID GetGeometry() const;
		MaterialVID GetMaterial() const;
	};

	struct API_EXPORT VzActorParticle : VzActor
	{
	};
}
