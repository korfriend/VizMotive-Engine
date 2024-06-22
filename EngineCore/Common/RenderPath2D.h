#pragma once
#include "RenderPath.h"

#include <vector>
#include <string>

namespace vz
{
	class Sprite;
	class SpriteFont;

	class RenderPath2D :
		public RenderPath
	{
	protected:
		vz::graphics::Texture rtStenciled_;
		vz::graphics::Texture rtStenciled_resolved_;
		vz::graphics::Texture rtFinal_;
		vz::graphics::Texture rtFinal_MSAA_;

		XMUINT2 currentBuffersize{};
		float currentLayoutscale{};

		float hdrScaling = 9.0f;

		uint32_t msaaSampleCount = 1;

	public:
		// Delete GPU resources and initialize them to default
		virtual void DeleteGPUResources();
		// create resolution dependent resources, such as render targets
		virtual void ResizeBuffers();
		// update DPI dependent elements, such as GUI elements, sprites
		virtual void ResizeLayout();

		void Update(float dt) override;
		void Render() const override;
		void Compose(vz::graphics::CommandList cmd) const override;

		virtual void setMSAASampleCount(uint32_t value) { msaaSampleCount = value; }
		constexpr uint32_t getMSAASampleCount() const { return msaaSampleCount; }

		const vz::graphics::Texture& GetRenderResult() const { return rtFinal_; }
		virtual const vz::graphics::Texture* GetDepthStencil() const { return nullptr; }
		virtual const vz::graphics::Texture* GetGUIBlurredBackground() const { return nullptr; }

		void AddSprite(vz::Sprite* sprite, const std::string& layer = "");
		void RemoveSprite(vz::Sprite* sprite);
		void ClearSprites();
		int GetSpriteOrder(vz::Sprite* sprite);

		void AddFont(vz::SpriteFont* font, const std::string& layer = "");
		void RemoveFont(vz::SpriteFont* font);
		void ClearFonts();
		int GetFontOrder(vz::SpriteFont* font);

		struct RenderItem2D
		{
			enum class TYPE
			{
				SPRITE,
				FONT,
			} type = TYPE::SPRITE;
			union
			{
				vz::Sprite* sprite = nullptr;
				vz::SpriteFont* font;
			};
			int order = 0;
		};
		struct RenderLayer2D
		{
			std::vector<RenderItem2D> items;
			std::string name;
			int order = 0;
		};
		std::vector<RenderLayer2D> layers{ 1 };
		void AddLayer(const std::string& name);
		void SetLayerOrder(const std::string& name, int order);
		void SetSpriteOrder(vz::Sprite* sprite, int order);
		void SetFontOrder(vz::SpriteFont* font, int order);
		void SortLayers();
		void CleanLayers();

		float resolutionScale = 1.0f;
		XMUINT2 GetInternalResolution() const
		{
			return XMUINT2(
				uint32_t((float)GetPhysicalWidth() * resolutionScale),
				uint32_t((float)GetPhysicalHeight() * resolutionScale)
			);
		}

		float GetHDRScaling() const { return hdrScaling; }
		void SetHDRScaling(float value) { hdrScaling = value; }
	};

}
