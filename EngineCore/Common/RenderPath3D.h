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

		struct GRenderPath3D;
	}
	// this renderer imports renderer-built binary (e.g., RendererDX11 or RendererDX12)
	class RenderPath3D : public RenderPath2D
	{
	protected:
		graphics::Texture rtRender3D_;	// main rt
		graphics::Texture rtRenderInterResult_;

		graphics::GRenderPath3D* handlerRenderPath3D_ = nullptr;

	public:
		RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice);
		~RenderPath3D() { DeleteGPUResources(); }

		CameraComponent* camera = nullptr;
		Scene* scene = nullptr;
		graphics::Viewport viewport;

		void DeleteGPUResources() override;
		void ResizeResources() override;
		// cpu side... in scene
		void Update(const float dt) override;
		void Render() override;
	};
}
