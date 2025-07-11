#pragma once
#include "GBackendDevice.h"
#include "Utils/vzMath.h"

#include <memory>
#include <limits>

namespace vz
{
	struct Scene;
	struct CameraComponent;

	struct GRenderPath2D
	{
		inline static const std::string GRenderPath2D_INTERFACE_VERSION = "GRenderPath2D::20250711";

	protected:
		graphics::SwapChain& swapChain_;	// same as the SwapChain in GRenderPath3D
		graphics::Texture& rtRenderFinal_;

		uint32_t layerMask_ = ~0u;

		// canvas size is supposed to be updated via ResizeCanvas()
		uint32_t canvasWidth_ = 0u;
		uint32_t canvasHeight_ = 0u;
	public:
		std::string version;

		GRenderPath2D(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
			: swapChain_(swapChain), rtRenderFinal_(rtRenderFinal) {
			version = GRenderPath2D_INTERFACE_VERSION;
		}
		virtual ~GRenderPath2D() = default;

		graphics::GraphicsDevice* device = nullptr;
		graphics::Rect scissor;
		graphics::ColorSpace colorspace = graphics::ColorSpace::SRGB;
		bool skipPostprocess = false;
		uint32_t msaaSampleCount = 1;

		uint32_t GetLayerMask() const { return layerMask_; }
		void SetlayerMask(uint32_t value) { layerMask_ = value; }

		virtual bool ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight) = 0; // must delete all canvas-related resources and re-create
		virtual bool Render2D(const float dt) = 0;
		virtual bool Destroy() = 0;
	};

	struct GRenderPath3D : GRenderPath2D
	{
		inline static const std::string GRenderPath3D_INTERFACE_VERSION = "GRenderPath3D::20250513";
		// this will be a component of vz::RenderPath3D

		enum class Tonemap
		{
			Reinhard,
			ACES
		};
		enum class DEBUG_BUFFER
		{
			NONE = 0,
			PRIMITIVE_ID,
			INSTANCE_ID,
			LINEAR_DEPTH,
			WITHOUT_POSTPROCESSING,
		};
	protected:

	public:

		GRenderPath3D(graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
			: GRenderPath2D(swapChain, rtRenderFinal) { 
			version = GRenderPath3D_INTERFACE_VERSION;
		}
		virtual ~GRenderPath3D() = default;

		Scene* scene = nullptr;
		CameraComponent* camera = nullptr;
		DEBUG_BUFFER debugMode = DEBUG_BUFFER::NONE;

		XMFLOAT4X4 matToScreen = math::IDENTITY_MATRIX;
		XMFLOAT4X4 matToScreenInv = math::IDENTITY_MATRIX;
		Tonemap tonemap = Tonemap::ACES;
		bool clearEnabled = true;
		graphics::Viewport viewport;
		size_t stableCount = 0;

		virtual bool ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight) override = 0; // must delete all canvas-related resources and re-create
		virtual bool Render(const float dt) = 0; 
		virtual bool Render2D(const float dt) override { return GRenderPath2D::Render2D(dt); }
		virtual bool Destroy() override = 0;
		virtual const graphics::Texture& GetLastProcessRT() const = 0;
	};

	struct GScene
	{
		inline static const std::string GScene_INTERFACE_VERSION = "GScene::20240921";
		// this will be a component of vz::Scene
	protected:
		Scene* scene_ = nullptr;
	public:
		std::string version = GScene_INTERFACE_VERSION;

		GScene(Scene* scene) : scene_(scene) {}
		virtual ~GScene() = default;

		virtual bool Update(const float dt) = 0;
		virtual bool Destroy() = 0;
	};
}