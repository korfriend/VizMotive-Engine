#include "VzEngineAPIs.h"
#include "Common/Engine_Internal.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"
#include "Utils/SimpleCollision.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_RENDERABLE_COMP(COMP, RET) RenderableComponent* COMP = compfactory::GetRenderableComponent(componentVID_); \
	if (!COMP) {post("RenderableComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzActor::SetVisibleLayerMask(const uint32_t visibleLayerMask, const bool includeDescendants)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetVisibleLayerMask(visibleLayerMask);

		if (includeDescendants)
		{
			std::vector<ActorVID> children = GetChildren();
			for (size_t i = 0, n = children.size(); i < n; ++i)
			{
				VzActor* base_actor = (VzActor*)vzm::GetComponent(children[i]);
				assert(base_actor);
				base_actor->SetVisibleLayerMask(visibleLayerMask, true);
			}
		}
		UpdateTimeStamp();
	}
	void VzActor::SetVisibleLayer(const bool visible, const uint32_t layerBits, const bool includeDescendants)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetVisibleLayer(visible, layerBits);

		if (includeDescendants)
		{
			std::vector<ActorVID> children = GetChildren();
			for (size_t i = 0, n = children.size(); i < n; ++i)
			{
				VzActor* base_actor = (VzActor*)vzm::GetComponent(children[i]);
				assert(base_actor);
				base_actor->SetVisibleLayer(visible, layerBits, true);
			}
		}
		UpdateTimeStamp();
	}
	uint32_t VzActor::GetVisibleLayerMask() const
	{
		GET_RENDERABLE_COMP(renderable, 0u);
		return renderable->GetVisibleLayerMask();
	}
	bool VzActor::IsVisibleWith(const uint32_t layerBits) const
	{
		GET_RENDERABLE_COMP(renderable, false);
		return renderable->IsVisibleWith(layerBits);
	}

	bool VzActor::IsRenderable() const
	{
		GET_RENDERABLE_COMP(renderable, false);
		return renderable->IsRenderable();
	}
	void VzActor::EnablePickable(const bool enabled, const bool includeDescendants)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnablePickable(enabled);
		
		if (includeDescendants)
		{
			std::vector<ActorVID> children = GetChildren();
			for (size_t i = 0, n = children.size(); i < n; ++i)
			{
				VzActor* base_actor = (VzActor*)vzm::GetComponent(children[i]);
				assert(base_actor);
				base_actor->EnablePickable(enabled, true);
			}
		}
		UpdateTimeStamp();
	}
	bool VzActor::IsPickable() const
	{
		GET_RENDERABLE_COMP(renderable, false);
		return renderable->IsPickable();
	}
}

namespace vzm
{
	void VzActorStaticMesh::SetGeometry(const GeometryVID vid)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetGeometry(vid);
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::SetMaterial(const MaterialVID vid, const int slot)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetMaterial(vid, slot);
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::SetMaterials(const std::vector<MaterialVID> vids)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetMaterials(vids);
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::EnableCastShadows(const bool enabled)
	{
		assert(0 && "TODO");
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::EnableReceiveShadows(const bool enabled)
	{
		assert(0 && "TODO");
		UpdateTimeStamp();
	}

	void VzActorStaticMesh::EnableSlicerSolidFill(const bool enabled)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnableSlicerSolidFill(enabled);
		UpdateTimeStamp();
	}

	void VzActorStaticMesh::EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnableClipper(clipBoxEnabled, clipPlaneEnabled);
		UpdateTimeStamp();
	}

	void VzActorStaticMesh::EnableOutline(const bool enabled)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnableOutline(enabled);
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::EnableUndercut(const bool enabled)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->EnableUndercut(enabled);
		UpdateTimeStamp();
	}

	void VzActorStaticMesh::SetClipPlane(const vfloat4& clipPlane)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetClipPlane(*(XMFLOAT4*)&clipPlane);
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::SetClipBox(const vfloat4x4& clipBox)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetClipBox(*(XMFLOAT4X4*)&clipBox);
		UpdateTimeStamp();
	}
	bool VzActorStaticMesh::IsClipperEnabled(bool* clipBoxEnabled, bool* clipPlaneEnabled) const
	{
		GET_RENDERABLE_COMP(renderable, false);
		bool box_clipped = renderable->IsBoxClipperEnabled();
		bool plane_clipped = renderable->IsPlaneClipperEnabled();
		if (clipBoxEnabled) *clipBoxEnabled = box_clipped;
		if (clipPlaneEnabled) *clipPlaneEnabled = plane_clipped;
		return box_clipped || plane_clipped;
	}

	void VzActorStaticMesh::SetOutineThickness(const float v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetOutineThickness(v);
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::SetOutineColor(const vfloat3 v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetOutineColor(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::SetOutineThreshold(const float v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetOutineThreshold(v);
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::SetUndercutDirection(const vfloat3 v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetUndercutDirection(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}
	void VzActorStaticMesh::SetUndercutColor(const vfloat3 v)
	{
		GET_RENDERABLE_COMP(renderable, );
		renderable->SetUndercutColor(*(XMFLOAT3*)&v);
		UpdateTimeStamp();
	}

	bool VzActorStaticMesh::CollisionCheck(const ActorVID targetActorVID, int* partIndexSrc, int* partIndexTarget, int* triIndexSrc, int* triIndexTarget) const
	{
		GET_RENDERABLE_COMP(renderable, false);
		RenderableComponent* renderable_target = compfactory::GetRenderableComponent(targetActorVID);
		if (renderable_target == nullptr)
		{
			vzlog_error("Invalid Target Actor!");
			return false;
		}
		int partIndexSrc_v, partIndexTarget_v, triIndexSrc_v, triIndexTarget_v;
		bool detected = bvhcollision::CollisionPairwiseCheck(renderable->GetGeometry(), componentVID_, renderable_target->GetGeometry(), targetActorVID, partIndexSrc_v, triIndexSrc_v, partIndexTarget_v, triIndexTarget_v);
		if (partIndexSrc) *partIndexSrc = partIndexSrc_v;
		if (partIndexTarget) *partIndexTarget = partIndexTarget_v;
		if (triIndexSrc) *triIndexSrc = triIndexSrc_v;
		if (triIndexTarget) *triIndexTarget = triIndexTarget_v;
		return detected;
	}

	void VzActorStaticMesh::DebugRender(const std::string& debugScript)
	{

	}

	std::vector<MaterialVID> VzActorStaticMesh::GetMaterials() const
	{
		GET_RENDERABLE_COMP(renderable, std::vector<MaterialVID>());
		return renderable->GetMaterials();
	}
	MaterialVID VzActorStaticMesh::GetMaterial(const int slot) const
	{
		GET_RENDERABLE_COMP(renderable, INVALID_VID);
		return renderable->GetMaterial(slot);
	}
	GeometryVID VzActorStaticMesh::GetGeometry() const
	{
		GET_RENDERABLE_COMP(renderable, INVALID_VID);
		return renderable->GetGeometry();
	}

	void VzActorStaticMesh::AssignCollider()
	{
		if (compfactory::ContainColliderComponent(componentVID_))
		{
			vzlog_warning("%s already has collider!", GetName().c_str());
			return;
		}
		compfactory::CreateColliderComponent(componentVID_);
	}

	bool VzActorStaticMesh::HasCollider() const
	{
		return compfactory::ContainColliderComponent(componentVID_);
	}

	bool VzActorStaticMesh::ColliderCollisionCheck(const ActorVID targetActorVID) const
	{
		assert(0 && "TODO");
		return false;
	}
}

namespace vzm
{
#define GET_SPRITE_COMP(COMP, RET) SpriteComponent* COMP = compfactory::GetSpriteComponent(componentVID_); \
	if (!COMP) {post("SpriteComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzActorSprite::SetSpriteTexture(const VID vidTexture)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->SetSpriteTexture(vidTexture);
		UpdateTimeStamp();
	}

	void VzActorSprite::EnableHidden(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableHidden(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableUpdate(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableUpdate(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableCameraFacing(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableCameraFacing(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableCameraScaling(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableCameraScaling(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableExtractNormalMap(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableExtractNormalMap(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableMirror(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableMirror(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableHDR10OutputMapping(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableHDR10OutputMapping(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableLinearOutputMapping(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableLinearOutputMapping(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableCornerRounding(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableCornerRounding(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableDepthTest(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableDepthTest(enabled);
		UpdateTimeStamp();
	}
	void VzActorSprite::EnableHighlight(bool enabled)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->EnableHighlight(enabled);
		UpdateTimeStamp();
	}


	bool VzActorSprite::IsDisableUpdate() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsDisableUpdate();
	}
	bool VzActorSprite::IsCameraFacing() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsCameraFacing();
	}
	bool VzActorSprite::IsCameraScaling() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsCameraScaling();
	}
	bool VzActorSprite::IsHidden() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsHidden();
	}
	bool VzActorSprite::IsExtractNormalMapEnabled() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsExtractNormalMapEnabled();
	}
	bool VzActorSprite::IsMirrorEnabled() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsMirrorEnabled();
	}
	bool VzActorSprite::IsHDR10OutputMappingEnabled() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsHDR10OutputMappingEnabled();
	}
	bool VzActorSprite::IsLinearOutputMappingEnabled() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsLinearOutputMappingEnabled();
	}
	bool VzActorSprite::IsCornerRoundingEnabled() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsCornerRoundingEnabled();
	}
	bool VzActorSprite::IsDepthTestEnabled() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsDepthTestEnabled();
	}
	bool VzActorSprite::IsHighlightEnabled() const
	{
		GET_SPRITE_COMP(sprite, false);
		return sprite->IsHighlightEnabled();
	}

	void VzActorSprite::SetSpritePosition(const vfloat3& p)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->SetPosition(*(XMFLOAT3*)&p);
		UpdateTimeStamp();
	}

	void VzActorSprite::SetSpriteScale(const vfloat2& s)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->SetScale(*(XMFLOAT2*)&s);
		UpdateTimeStamp();
	}
	void VzActorSprite::SetSpriteUVOffset(const vfloat2& uvOffset)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->SetUVOffset(*(XMFLOAT2*)&uvOffset);
		UpdateTimeStamp();
	}
	void VzActorSprite::SetSpriteRotation(const float v)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->SetRotation(v);
		UpdateTimeStamp();
	}
	void VzActorSprite::SetSpriteOpacity(const float v)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->SetOpacity(v);
		UpdateTimeStamp();
	}
	void VzActorSprite::SetSpriteFade(const float v)
	{
		GET_SPRITE_COMP(sprite, );
		sprite->SetFade(v);
		UpdateTimeStamp();
	}
}

namespace vzm
{
#define GET_SPRITEFONT_COMP(COMP, RET) SpriteFontComponent* COMP = compfactory::GetSpriteFontComponent(componentVID_); \
	if (!COMP) {post("SpriteFontComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzActorSpriteFont::EnableHidden(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableHidden(enabled);
	}
	void VzActorSpriteFont::EnableUpdate(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableUpdate(enabled);
	}
	void VzActorSpriteFont::EnableCameraFacing(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableCameraFacing(enabled);
	}
	void VzActorSpriteFont::EnableCameraScaling(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableCameraScaling(enabled);
	}
	void VzActorSpriteFont::EnableSDFRendering(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableSDFRendering(enabled);
	}
	void VzActorSpriteFont::EnableHDR10OutputMapping(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableHDR10OutputMapping(enabled);
	}
	void VzActorSpriteFont::EnableLinearOutputMapping(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableLinearOutputMapping(enabled);
	}
	void VzActorSpriteFont::EnableDepthTest(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableDepthTest(enabled);
	}
	void VzActorSpriteFont::EnableFlipHorizontally(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableFlipHorizontally(enabled);
	}
	void VzActorSpriteFont::EnableFlipVertically(bool enabled)
	{
		GET_SPRITEFONT_COMP(font, );
		font->EnableFlipVertically(enabled);
	}

	bool VzActorSpriteFont::IsDisableUpdate() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsDisableUpdate();
	}
	bool VzActorSpriteFont::IsCameraFacing() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsCameraFacing();
	}
	bool VzActorSpriteFont::IsCameraScaling() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsCameraScaling();
	}
	bool VzActorSpriteFont::IsHidden() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsHidden();
	}
	bool VzActorSpriteFont::IsSDFRendering() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsSDFRendering();
	}
	bool VzActorSpriteFont::IsHDR10OutputMapping() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsHDR10OutputMapping();
	}
	bool VzActorSpriteFont::IsLinearOutputMapping() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsLinearOutputMapping();
	}
	bool VzActorSpriteFont::IsDepthTest() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsDepthTest();
	}
	bool VzActorSpriteFont::IsFlipHorizontally() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsFlipHorizontally();
	}
	bool VzActorSpriteFont::IsFlipVertically() const
	{
		GET_SPRITEFONT_COMP(font, false);
		return font->IsFlipVertically();
	}

	void VzActorSpriteFont::SetText(const std::string& text)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetText(text);
	}
	void VzActorSpriteFont::SetFontStyle(const std::string& fontStyle)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetFontStyle(fontStyle);
	}
	void VzActorSpriteFont::SetFontSize(const int size)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetSize(size);
	}
	void VzActorSpriteFont::SetFontScale(const float scale)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetScale(scale);
	}
	void VzActorSpriteFont::SetSpacing(const vfloat2& spacing)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetSpacing(*(XMFLOAT2*)&spacing);
	}
	void VzActorSpriteFont::SetHorizonAlign(const VzActorSpriteFont::Alignment horizonAlign)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetHorizonAlign(static_cast<SpriteFontComponent::Alignment>(horizonAlign));
	}
	void VzActorSpriteFont::SetVerticalAlign(const VzActorSpriteFont::Alignment verticalAlign)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetVerticalAlign(static_cast<SpriteFontComponent::Alignment>(verticalAlign));
	}
	void VzActorSpriteFont::SetColor(const vfloat4& color)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetColor(*(XMFLOAT4*)&color);
	}
	void VzActorSpriteFont::SetShadowColor(const vfloat4& shadowColor)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetShadowColor(*(XMFLOAT4*)&shadowColor);
	}
	void VzActorSpriteFont::SetWrap(const float wrap)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetWrap(wrap);
	}
	void VzActorSpriteFont::SetSoftness(const float softness)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetSoftness(softness);
	}
	void VzActorSpriteFont::SetBolden(const float bolden)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetBolden(bolden);
	}
	void VzActorSpriteFont::SetShadowSoftness(const float shadowSoftness)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetShadowSoftness(shadowSoftness);
	}
	void VzActorSpriteFont::SetShadowBolden(const float shadowBolden)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetShadowBolden(shadowBolden);
	}
	void VzActorSpriteFont::SetShadowOffset(const vfloat2 shadowOffset)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetShadowOffset(*(XMFLOAT2*)&shadowOffset);
	}
	void VzActorSpriteFont::SetHdrScale(const float hdrScaling)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetHdrScale(hdrScaling);
	}
	void VzActorSpriteFont::SetIntensity(const float intensity)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetIntensity(intensity);
	}
	void VzActorSpriteFont::SetShadowIntensity(const float shadowIntensity)
	{
		GET_SPRITEFONT_COMP(font, );
		font->SetShadowIntensity(shadowIntensity);
	}
}