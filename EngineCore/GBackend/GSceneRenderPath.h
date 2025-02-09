#pragma once
#include "GBackendDevice.h"
#include "Utils/vzMath.h"

#include <memory>
#include <limits>

namespace vz
{
	struct Scene;
	struct CameraComponent;

	struct GRenderPath3D
	{
		inline static const std::string GRenderPath3D_INTERFACE_VERSION = "GRenderPath3D::20241205";
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
		graphics::Viewport& viewport_;
		graphics::SwapChain& swapChain_;
		graphics::Texture& rtRenderFinal_;

		// canvas size is supposed to be updated via ResizeCanvas()
		uint32_t canvasWidth_ = 1u;
		uint32_t canvasHeight_ = 1u;
	public:
		std::string version = GRenderPath3D_INTERFACE_VERSION;

		GRenderPath3D(graphics::Viewport& vp, graphics::SwapChain& swapChain, graphics::Texture& rtRenderFinal)
			: viewport_(vp), swapChain_(swapChain), rtRenderFinal_(rtRenderFinal) {}

		Scene* scene = nullptr;
		CameraComponent* camera = nullptr;
		DEBUG_BUFFER debugMode = DEBUG_BUFFER::NONE;

		XMFLOAT4X4 matToScreen = math::IDENTITY_MATRIX;
		XMFLOAT4X4 matToScreenInv = math::IDENTITY_MATRIX;

		graphics::Viewport viewport;
		graphics::Rect scissor;
		graphics::ColorSpace colorspace = graphics::ColorSpace::SRGB;
		Tonemap tonemap = Tonemap::ACES;
		uint32_t msaaSampleCount = 1;

		virtual bool ResizeCanvas(uint32_t canvasWidth, uint32_t canvasHeight) = 0; // must delete all canvas-related resources and re-create
		virtual bool Render(const float dt) = 0;
		virtual bool Destroy() = 0;
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

		virtual bool Update(const float dt) = 0;
		virtual bool Destroy() = 0;
	};
}