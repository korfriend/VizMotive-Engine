#pragma once
#include "RenderPath2D.h"

namespace vz
{
	namespace graphics
	{
		constexpr Format formatDepthbufferMain = graphics::Format::D32_FLOAT_S8X24_UINT;
		constexpr Format formatRendertargetMain = graphics::Format::R11G11B10_FLOAT;
		constexpr Format formatIdbuffer = graphics::Format::R32_UINT;
		constexpr Format formatRendertargetShadowmap = graphics::Format::R16G16B16A16_FLOAT;
		constexpr Format formatDepthbufferShadowmap = graphics::Format::D16_UNORM;
		constexpr Format formatRendertargetEnvprobe = graphics::Format::R11G11B10_FLOAT;
		constexpr Format formatDepthbufferEnvprobe = graphics::Format::D16_UNORM;
	}
	struct GRenderPath3D;
	// this renderer imports renderer-built binary (e.g., RendererDX11 or RendererDX12)
	class RenderPath3D : public RenderPath2D
	{
	protected:
		GRenderPath3D* handlerRenderPath3D_ = nullptr;

		graphics::Viewport viewport_;
		graphics::Rect scissor_;

		XMFLOAT4X4 matScreen_ = math::IDENTITY_MATRIX;
		XMFLOAT4X4 matScreenInv_ = math::IDENTITY_MATRIX;

		void updateViewportTransforms();

	public:
		RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice);
		~RenderPath3D();

		CameraComponent* camera = nullptr; // can be used for SlicerComponent
		Scene* scene = nullptr;

		bool useManualSetViewport = false;
		bool useManualSetScissor = false;

		const graphics::Viewport& GetViewport() const { return viewport_; }
		void SetViewport(const graphics::Viewport& vp) {
			viewport_ = vp; updateViewportTransforms();
		}

		void DeleteGPUResources(const bool resizableOnly) override;
		void ResizeResources() override;
		// cpu side... in scene
		void Update(const float dt) override;
		void Render(const float dt) override;
		void Compose() override;
		
		void GetViewportTransforms(XMFLOAT4X4* matScreen, XMFLOAT4X4* matScreenInv) const {
			if (matScreen) *matScreen = matScreen_;
			if (matScreenInv) *matScreenInv = matScreenInv_;
		}

		void ShowDebugBuffer(const std::string& debugMode = "NONE");

		geometrics::Ray GetPickRay(float screenX, float screenY, const CameraComponent& camera);
	};
}
