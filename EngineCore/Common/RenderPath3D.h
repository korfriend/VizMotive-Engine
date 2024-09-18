#pragma once
#include "RenderPath2D.h"

namespace vz
{
	// this renderer imports renderer-built binary (e.g., RendererDX11 or RendererDX12)
	class RenderPath3D : public RenderPath2D
	{
	protected:
		void tryResizeRenderTargets() override;
	public:
		CameraComponent* camera = nullptr;
		Scene* scene = nullptr;

		typedef bool(*PI_RenderMesh)(Scene* scene, CameraComponent* camera);
		PI_RenderMesh RenderMesh = nullptr;

		void Render() const override;
	};
}
