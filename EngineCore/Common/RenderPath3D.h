#pragma once
#include "RenderPath2D.h"

namespace vz
{
	// this renderer imports renderer-built binary (e.g., RendererDX11 or RendererDX12)
	class RenderPath3D : public RenderPath2D
	{
	protected:
		graphics::Texture rtRender3D_;
		graphics::Texture rtRenderInterResult_;

	public:
		RenderPath3D(const Entity entity, graphics::GraphicsDevice* graphicsDevice) 
			: RenderPath2D(entity, graphicsDevice_) {}
		~RenderPath3D() { DeleteGPUResources(); }

		CameraComponent* camera = nullptr;
		Scene* scene = nullptr;

		typedef bool(*PI_RenderMesh)(Scene* scene, CameraComponent* camera);
		PI_RenderMesh RenderMesh = nullptr;

		void DeleteGPUResources() override;
		void ResizeResources() override;
		void Update() const override;
		void Render() const override;
	};
}
