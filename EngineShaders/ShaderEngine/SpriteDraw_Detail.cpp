#include "RenderPath3D_Detail.h"
#include "Image.h"
#include "Font.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::DrawSpritesAndFonts(
		const CameraComponent& camera,
		bool distortion,
		CommandList cmd
	)
	{
		if (scene_Gdetails->spriteComponents.size() == 0 && scene_Gdetails->spriteFontComponents.size() == 0)
			return;

		LayeredMaskComponent* layeredmask = camera.GetLayeredMaskComponent();

		std::string process_name = "Sprites and Fonts";
		if (distortion)
		{
			process_name += "(distortion)";
		}
		device->EventBegin(process_name.c_str(), cmd);
		auto range = profiler::BeginRangeGPU(process_name.c_str(), &cmd);

		const XMMATRIX VP = XMLoadFloat4x4(&camera.GetViewProjection());
		const XMMATRIX R = XMLoadFloat3x3(&camera.GetRotationToFaceCamera());

		enum TYPE // ordering of the enums is important, it is designed to prioritize font on top of sprite rendering if distance is the same
		{
			FONT,
			SPRITE,
		};
		union DistanceSorter {
			struct
			{
				uint64_t id : 32;
				uint64_t type : 16;
				uint64_t distance : 16;
			} bits;
			uint64_t raw;
			static_assert(sizeof(bits) == sizeof(raw));
		};
		static thread_local std::vector<uint64_t> distance_sorter;
		distance_sorter.clear();
		for (size_t i = 0, n = scene_Gdetails->spriteComponents.size(); i < n; ++i)
		{
			GRenderableComponent* renderable = scene_Gdetails->spriteComponents[i];
			GSpriteComponent* sprite = renderable->sprite;
			if (sprite->IsHidden() ||
				!renderable->layeredmask->IsVisibleWith(layeredmask->GetVisibleLayerMask())
				)
				continue;
			if (sprite->IsExtractNormalMapEnabled() != distortion)
				continue;
			DistanceSorter sorter = {};
			sorter.bits.id = uint32_t(i);
			sorter.bits.type = SPRITE;
			sorter.bits.distance = XMConvertFloatToHalf(math::DistanceEstimated(sprite->posW, camera.GetWorldEye()));
			distance_sorter.push_back(sorter.raw);
		}
		if (!distortion)
		{
			for (size_t i = 0, n = scene_Gdetails->spriteFontComponents.size(); i < n; ++i)
			{
				GRenderableComponent* renderable = scene_Gdetails->spriteFontComponents[i];
				GSpriteFontComponent* font = renderable->spritefont;
				if (font->IsHidden() ||
					!renderable->layeredmask->IsVisibleWith(layeredmask->GetVisibleLayerMask())
					)
					continue;
				DistanceSorter sorter = {};
				sorter.bits.id = uint32_t(i);
				sorter.bits.type = FONT;
				sorter.bits.distance = XMConvertFloatToHalf(math::DistanceEstimated(font->posW, camera.GetWorldEye()));
				distance_sorter.push_back(sorter.raw);
			}
		}

		std::sort(distance_sorter.begin(), distance_sorter.end(), std::greater<uint64_t>());
		for (auto& x : distance_sorter)
		{
			DistanceSorter sorter = {};
			sorter.raw = x;
			XMMATRIX M = VP;
			Entity entity = INVALID_ENTITY;

			switch (sorter.bits.type)
			{
			default:
			case SPRITE:
			{
				GSpriteComponent* sprite = scene_Gdetails->spriteComponents[sorter.bits.id]->sprite;

				vz::image::Params params;
				params.pos = sprite->GetPosition();
				params.rotation = sprite->GetRotation();
				params.scale = sprite->GetScale();
				//params.size = sprite->GetSize(); // TODO or from Texture size?!
				params.opacity = sprite->GetOpacity();
				params.texOffset = sprite->GetUVOffset();

				sprite->IsExtractNormalMapEnabled() ? params.enableExtractNormalMap() : params.disableExtractNormalMap();
				sprite->IsMirrorEnabled() ? params.enableMirror() : params.disableMirror();
				sprite->IsHDR10OutputMappingEnabled() ? params.enableHDR10OutputMapping() : params.disableHDR10OutputMapping();
				sprite->IsLinearOutputMappingEnabled() ? params.enableLinearOutputMapping() : params.disableLinearOutputMapping();
				sprite->IsCornerRoundingEnabled() ? params.enableCornerRounding() : params.disableCornerRounding();
				sprite->IsDepthTestEnabled() ? params.enableDepthTest() : params.disableDepthTest();
				sprite->IsHighlightEnabled() ? params.enableHighlight() : params.disableHighlight();

				if (sprite->IsCameraScaling())
				{
					float scale = 0.05f * math::Distance(sprite->posW, camera.GetWorldEye());
					params.scale = XMFLOAT2(params.scale.x * scale, params.scale.y * scale);
				}

				if (sprite->IsCameraFacing())
				{
					M = XMMatrixScaling(sprite->scaleW.x, sprite->scaleW.y, sprite->scaleW.z) * R *
						XMMatrixTranslation(sprite->translateW.x, sprite->translateW.y, sprite->translateW.z) * M;
				}
				else
				{
					M = sprite->W * M;
				}
				params.customProjection = &M;
				//if (sprite.maskResource.IsValid())
				//{
				//	params.setMaskMap(&sprite.maskResource.GetTexture());
				//}
				//else
				//{
				//	params.setMaskMap(nullptr);
				//}

				params.enableSprite();
				GTextureComponent* texture = sprite->texture;
				image::Draw(texture ? &texture->resource.GetTexture() : nullptr, params, cmd);
			}
			break;
			case FONT:
			{
				GSpriteFontComponent* font_sprite = scene_Gdetails->spriteFontComponents[sorter.bits.id]->spritefont;

				vz::font::Params params;
				params.position = font_sprite->GetPosition();
				params.rotation = font_sprite->GetRotation();
				params.scaling = font_sprite->GetScale();
				params.color.fromFloat4(font_sprite->GetColor());

				// params TODO attributes setting
				//font_sprite->GetFontStyle(); // params.style
				params.size = font_sprite->GetSize();
				XMFLOAT2 spacing = font_sprite->GetSpacing();
				params.spacingX = spacing.x;
				params.spacingY = spacing.y;
				params.h_align = font_sprite->GetHorizonAlign();
				params.v_align = font_sprite->GetVerticalAlign();
				params.color.fromFloat4(font_sprite->GetColor());
				params.shadowColor.fromFloat4(font_sprite->GetShadowColor());
				params.h_wrap = font_sprite->GetWrap();
				params.softness = font_sprite->GetSoftness();
				params.bolden = font_sprite->GetBolden();
				params.shadow_softness = font_sprite->GetShadowSoftness();
				params.shadow_bolden = font_sprite->GetShadowBolden();
				XMFLOAT2 shadow_offset = font_sprite->GetShadowOffset();
				params.shadow_offset_x = shadow_offset.x;
				params.shadow_offset_y = shadow_offset.y;
				params.hdr_scaling = font_sprite->GetHdrScale();
				params.intensity = font_sprite->GetIntensity();
				params.shadow_intensity = font_sprite->GetShadowIntensity();
				params.cursor = font_sprite->GetCursor();

				if (font_sprite->IsCameraScaling())
				{
					float scale = 0.05f * math::Distance(font_sprite->posW, camera.GetWorldEye());
					params.scaling *= scale;
				}

				if (font_sprite->IsCameraFacing())
				{
					M = XMMatrixScaling(font_sprite->scaleW.x, font_sprite->scaleW.y, font_sprite->scaleW.z) * R *
						XMMatrixTranslation(font_sprite->translateW.x, font_sprite->translateW.y, font_sprite->translateW.z) * M;
				}
				else
				{
					M = font_sprite->W * M;
				}

				params.customProjection = &M;
				vz::font::Draw(font_sprite->GetText().c_str(), font_sprite->GetCurrentTextLength(), params, cmd);
			}
			break;
			}
		}
		profiler::EndRange(range);
		device->EventEnd(cmd);
	}
}