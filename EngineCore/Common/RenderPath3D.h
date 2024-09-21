#pragma once
#include "RenderPath2D.h"

namespace vz
{
	constexpr graphics::Format formatDepthbufferMain = graphics::Format::D32_FLOAT_S8X24_UINT;
	constexpr graphics::Format formatRendertargetMain = graphics::Format::R11G11B10_FLOAT;
	constexpr graphics::Format formatIdbuffer = graphics::Format::R32_UINT;
	constexpr graphics::Format formatRendertargetShadowmap = graphics::Format::R16G16B16A16_FLOAT;
	constexpr graphics::Format formatDepthbufferShadowmap = graphics::Format::D16_UNORM;
	constexpr graphics::Format formatRendertargetEnvprobe = graphics::Format::R11G11B10_FLOAT;
	constexpr graphics::Format formatDepthbufferEnvprobe = graphics::Format::D16_UNORM;

	// this renderer imports renderer-built binary (e.g., RendererDX11 or RendererDX12)
	class RenderPath3D : public RenderPath2D
	{
	protected:
		graphics::Texture rtRender3D_;	// main rt
		graphics::Texture rtRenderInterResult_;

		void* pluginHandler_ = nullptr;

	public:
		RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice);
		~RenderPath3D() { DeleteGPUResources(); }

		CameraComponent* camera = nullptr;
		Scene* scene = nullptr;

		inline void SetPluginHandler(void* pluginHandler) { pluginHandler_ = pluginHandler; }
		inline void* GetPluginHandler() { return pluginHandler_; }

		void DeleteGPUResources() override;
		void ResizeResources() override;
		// cpu side... in scene
		void Update(const float dt) override;
		void Render() override;
	};
}
