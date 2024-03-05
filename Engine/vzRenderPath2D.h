#pragma once
#include "vzRenderPath.h"
#include "vzGUI.h"
#include "vzVector.h"

#include <string>

namespace vz
{
	class Sprite;
	class SpriteFont;

	class RenderPath2D :
		public RenderPath
	{
	private:
		vz::graphics::Texture rtStenciled;
		vz::graphics::Texture rtStenciled_resolved;
		vz::graphics::Texture rtFinal;

		vz::gui::GUI GUI;

		XMUINT2 current_buffersize{};
		float current_layoutscale{};

		float hdr_scaling = 9.0f;

	public:
		// Delete GPU resources and initialize them to default
		virtual void DeleteGPUResources();
		// create resolution dependent resources, such as render targets
		virtual void ResizeBuffers();
		// update DPI dependent elements, such as GUI elements, sprites
		virtual void ResizeLayout();

		void Update(float dt) override;
		void FixedUpdate() override;
		void Render() const override;
		void Compose(vz::graphics::CommandList cmd) const override;

		const vz::graphics::Texture& GetRenderResult() const { return rtFinal; }
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
			vz::vector<RenderItem2D> items;
			std::string name;
			int order = 0;
		};
		vz::vector<RenderLayer2D> layers{ 1 };
		void AddLayer(const std::string& name);
		void SetLayerOrder(const std::string& name, int order);
		void SetSpriteOrder(vz::Sprite* sprite, int order);
		void SetFontOrder(vz::SpriteFont* font, int order);
		void SortLayers();
		void CleanLayers();

		const vz::gui::GUI& GetGUI() const { return GUI; }
		vz::gui::GUI& GetGUI() { return GUI; }

		float resolutionScale = 1.0f;
		XMUINT2 GetInternalResolution() const
		{
			return XMUINT2(
				uint32_t((float)GetPhysicalWidth() * resolutionScale),
				uint32_t((float)GetPhysicalHeight() * resolutionScale)
			);
		}

		float GetHDRScaling() const { return hdr_scaling; }
		void SetHDRScaling(float value) { hdr_scaling = value; }
	};

}
