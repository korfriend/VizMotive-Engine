#pragma once
#include "vzRenderPath3D.h"
#include "vzVector.h"

namespace vz
{

	class RenderPath3D_PathTracing :
		public RenderPath3D
	{
	protected:
		int sam = -1;
		int target = 1024;
		vz::graphics::Texture traceResult;
		vz::graphics::Texture traceDepth;
		vz::graphics::Texture traceStencil;

		vz::vector<uint8_t> texturedata_src;
		vz::vector<uint8_t> texturedata_dst;
		vz::vector<uint8_t> texturedata_albedo;
		vz::vector<uint8_t> texturedata_normal;
		vz::graphics::Texture denoiserAlbedo;
		vz::graphics::Texture denoiserNormal;
		vz::graphics::Texture denoiserResult;
		vz::jobsystem::context denoiserContext;

		void ResizeBuffers() override;

	public:

		void Update(float dt) override;
		void Render() const override;
		void Compose(vz::graphics::CommandList cmd) const override;

		int getCurrentSampleCount() const { return sam; }
		void setTargetSampleCount(int value) { target = value; }
		float getProgress() const { return (float)sam / (float)target; }

		float denoiserProgress = 0;
		float getDenoiserProgress() const { return denoiserProgress; }
		bool isDenoiserAvailable() const;

		void resetProgress() { sam = -1; denoiserProgress = 0; volumetriccloudResources.frame = 0; }

		uint8_t instanceInclusionMask_PathTrace = 0xFF;
	};

}
