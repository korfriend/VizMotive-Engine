#pragma once
#include "vzRenderPath2D.h"
#include "vzRenderer.h"
#include "vzGraphicsDevice.h"
#include "vzResourceManager.h"
#include "vzScene.h"

namespace vz
{

	class RenderPath3D :
		public RenderPath2D
	{
	public:
		enum AO
		{
			AO_DISABLED,	// no ambient occlusion
			AO_SSAO,		// simple brute force screen space ambient occlusion
			AO_HBAO,		// horizon based screen space ambient occlusion
			AO_MSAO,		// multi scale screen space ambient occlusion
			AO_RTAO,		// ray traced ambient occlusion
			// Don't alter order! (bound to lua manually)
		};
		enum class FSR2_Preset
		{
			// Guidelines: https://github.com/GPUOpen-Effects/FidelityFX-FSR2#scaling-modes
			Quality,
			Balanced,
			Performance,
			Ultra_Performance,
		};
	private:
		float exposure = 1.0f;
		float brightness = 0.0f;
		float contrast = 1.0f;
		float saturation = 1.0f;
		float bloomThreshold = 1.0f;
		float motionBlurStrength = 100.0f;
		float dofStrength = 10.0f;
		float sharpenFilterAmount = 0.28f;
		float outlineThreshold = 0.2f;
		float outlineThickness = 1.0f;
		XMFLOAT4 outlineColor = XMFLOAT4(0, 0, 0, 1);
		float aoRange = 1.0f;
		uint32_t aoSampleCount = 16;
		float aoPower = 1.0f;
		float chromaticAberrationAmount = 2.0f;
		uint32_t screenSpaceShadowSampleCount = 16;
		float screenSpaceShadowRange = 1;
		float eyeadaptionKey = 0.115f;
		float eyeadaptionRate = 1;
		float fsrSharpness = 1.0f;
		float fsr2Sharpness = 0.5f;
		float lightShaftsStrength = 0.2f;
		float raytracedDiffuseRange = 10;
		float raytracedReflectionsRange = 10000.0f;
		float reflectionRoughnessCutoff = 0.6f;
		vz::renderer::Tonemap tonemap = vz::renderer::Tonemap::ACES;

		AO ao = AO_DISABLED;
		bool fxaaEnabled = false;
		bool ssrEnabled = false;
		bool raytracedReflectionsEnabled = false;
		bool raytracedDiffuseEnabled = false;
		bool reflectionsEnabled = true;
		bool shadowsEnabled = true;
		bool bloomEnabled = true;
		bool colorGradingEnabled = true;
		bool volumeLightsEnabled = true;
		bool lightShaftsEnabled = false;
		bool lensFlareEnabled = true;
		bool motionBlurEnabled = false;
		bool depthOfFieldEnabled = true;
		bool eyeAdaptionEnabled = false;
		bool sharpenFilterEnabled = false;
		bool outlineEnabled = false;
		bool chromaticAberrationEnabled = false;
		bool ditherEnabled = true;
		bool occlusionCullingEnabled = true;
		bool sceneUpdateEnabled = true;
		bool fsrEnabled = false;
		bool fsr2Enabled = false;

		uint32_t msaaSampleCount = 1;

	public:
		vz::graphics::Texture rtMain;
		vz::graphics::Texture rtMain_render; // can be MSAA
		vz::graphics::Texture rtPrimitiveID;
		vz::graphics::Texture rtPrimitiveID_render; // can be MSAA
		vz::graphics::Texture rtVelocity; // optional R16G16_FLOAT
		vz::graphics::Texture rtReflection; // contains the scene rendered for planar reflections
		vz::graphics::Texture rtRaytracedDiffuse; // raytraced diffuse screen space texture
		vz::graphics::Texture rtSSR; // standard screen-space reflection results
		vz::graphics::Texture rtSceneCopy; // contains the rendered scene that can be fed into transparent pass for distortion effect
		vz::graphics::Texture rtSceneCopy_tmp; // temporary for gaussian mipchain
		vz::graphics::Texture rtWaterRipple; // water ripple sprite normal maps are rendered into this
		vz::graphics::Texture rtParticleDistortion; // contains distortive particles
		vz::graphics::Texture rtParticleDistortion_Resolved; // contains distortive particles
		vz::graphics::Texture rtVolumetricLights[2]; // contains the volumetric light results
		vz::graphics::Texture rtBloom; // contains the bright parts of the image + mipchain
		vz::graphics::Texture rtBloom_tmp; // temporary for bloom downsampling
		vz::graphics::Texture rtAO; // full res AO
		vz::graphics::Texture rtShadow; // raytraced shadows mask
		vz::graphics::Texture rtSun[2]; // 0: sun render target used for lightshafts (can be MSAA), 1: radial blurred lightshafts
		vz::graphics::Texture rtSun_resolved; // sun render target, but the resolved version if MSAA is enabled
		vz::graphics::Texture rtGUIBlurredBackground[3];	// downsampled, gaussian blurred scene for GUI
		vz::graphics::Texture rtShadingRate; // UINT8 shading rate per tile
		vz::graphics::Texture rtFSR[2]; // FSR upscaling result (full resolution LDR)
		vz::graphics::Texture rtOutlineSource; // linear depth but only the regions which have outline stencil

		vz::graphics::Texture rtPostprocess; // ping-pong with main scene RT in post-process chain

		vz::graphics::Texture depthBuffer_Main; // used for depth-testing, can be MSAA
		vz::graphics::Texture depthBuffer_Copy; // used for shader resource, single sample
		vz::graphics::Texture depthBuffer_Copy1; // used for disocclusion check
		vz::graphics::Texture depthBuffer_Reflection; // used for reflection, single sample
		vz::graphics::Texture rtLinearDepth; // linear depth result + mipchain (max filter)

		vz::graphics::Texture debugUAV; // debug UAV can be used by some shaders...
		vz::renderer::TiledLightResources tiledLightResources;
		vz::renderer::TiledLightResources tiledLightResources_planarReflection;
		vz::renderer::LuminanceResources luminanceResources;
		vz::renderer::SSAOResources ssaoResources;
		vz::renderer::MSAOResources msaoResources;
		vz::renderer::RTAOResources rtaoResources;
		vz::renderer::RTDiffuseResources rtdiffuseResources;
		vz::renderer::RTReflectionResources rtreflectionResources;
		vz::renderer::SSRResources ssrResources;
		vz::renderer::RTShadowResources rtshadowResources;
		vz::renderer::ScreenSpaceShadowResources screenspaceshadowResources;
		vz::renderer::DepthOfFieldResources depthoffieldResources;
		vz::renderer::MotionBlurResources motionblurResources;
		vz::renderer::AerialPerspectiveResources aerialperspectiveResources;
		vz::renderer::AerialPerspectiveResources aerialperspectiveResources_reflection;
		vz::renderer::VolumetricCloudResources volumetriccloudResources;
		vz::renderer::VolumetricCloudResources volumetriccloudResources_reflection;
		vz::renderer::BloomResources bloomResources;
		vz::renderer::SurfelGIResources surfelGIResources;
		vz::renderer::TemporalAAResources temporalAAResources;
		vz::renderer::VisibilityResources visibilityResources;
		vz::renderer::FSR2Resources fsr2Resources;
		vz::renderer::VXGIResources vxgiResources;

		vz::graphics::CommandList video_cmd;
		vz::vector<vz::video::VideoInstance*> video_instances_tmp;

		mutable const vz::graphics::Texture* lastPostprocessRT = &rtPostprocess;
		// Post-processes are ping-ponged, this function helps to obtain the last postprocess render target that was written
		const vz::graphics::Texture* GetLastPostprocessRT() const
		{
			return lastPostprocessRT;
		}

		virtual void RenderAO(vz::graphics::CommandList cmd) const;
		virtual void RenderSSR(vz::graphics::CommandList cmd) const;
		virtual void RenderOutline(vz::graphics::CommandList cmd) const;
		virtual void RenderLightShafts(vz::graphics::CommandList cmd) const;
		virtual void RenderVolumetrics(vz::graphics::CommandList cmd) const;
		virtual void RenderSceneMIPChain(vz::graphics::CommandList cmd) const;
		virtual void RenderTransparents(vz::graphics::CommandList cmd) const;
		virtual void RenderPostprocessChain(vz::graphics::CommandList cmd) const;

		void DeleteGPUResources() override;
		void ResizeBuffers() override;

		vz::scene::CameraComponent* camera = &vz::scene::GetCamera();
		vz::scene::CameraComponent camera_previous;
		vz::scene::CameraComponent camera_reflection;
		vz::scene::CameraComponent camera_reflection_previous;

		vz::scene::Scene* scene = &vz::scene::GetScene();
		vz::renderer::Visibility visibility_main;
		vz::renderer::Visibility visibility_reflection;

		FrameCB frameCB = {};

		bool visibility_shading_in_compute = false;

		// Crop parameters in logical coordinates:
		float crop_left = 0;
		float crop_top = 0;
		float crop_right = 0;
		float crop_bottom = 0;
		vz::graphics::Rect GetScissorNativeResolution() const
		{
			vz::graphics::Rect scissor;
			scissor.left = int(LogicalToPhysical(crop_left));
			scissor.top = int(LogicalToPhysical(crop_top));
			scissor.right = int(GetPhysicalWidth() - LogicalToPhysical(crop_right));
			scissor.bottom = int(GetPhysicalHeight() - LogicalToPhysical(crop_bottom));
			return scissor;
		}
		vz::graphics::Rect GetScissorInternalResolution() const
		{
			vz::graphics::Rect scissor;
			scissor.left = int(LogicalToPhysical(crop_left) * resolutionScale);
			scissor.top = int(LogicalToPhysical(crop_top) * resolutionScale);
			scissor.right = int(GetInternalResolution().x - LogicalToPhysical(crop_right) * resolutionScale);
			scissor.bottom = int(GetInternalResolution().y - LogicalToPhysical(crop_bottom) * resolutionScale);
			return scissor;
		}

		const vz::graphics::Texture* GetDepthStencil() const override { return &depthBuffer_Main; }
		const vz::graphics::Texture* GetGUIBlurredBackground() const override { return &rtGUIBlurredBackground[2]; }

		constexpr float getExposure() const { return exposure; }
		constexpr float getBrightness() const { return brightness; }
		constexpr float getContrast() const { return contrast; }
		constexpr float getSaturation() const { return saturation; }
		constexpr float getBloomThreshold() const { return bloomThreshold; }
		constexpr float getMotionBlurStrength() const { return motionBlurStrength; }
		constexpr float getDepthOfFieldStrength() const { return dofStrength; }
		constexpr float getSharpenFilterAmount() const { return sharpenFilterAmount; }
		constexpr float getOutlineThreshold() const { return outlineThreshold; }
		constexpr float getOutlineThickness() const { return outlineThickness; }
		constexpr XMFLOAT4 getOutlineColor() const { return outlineColor; }
		constexpr float getAORange() const { return aoRange; }
		constexpr uint32_t getAOSampleCount() const { return aoSampleCount; }
		constexpr float getAOPower() const { return aoPower; }
		constexpr float getChromaticAberrationAmount() const { return chromaticAberrationAmount; }
		constexpr uint32_t getScreenSpaceShadowSampleCount() const { return screenSpaceShadowSampleCount; }
		constexpr float getScreenSpaceShadowRange() const { return screenSpaceShadowRange; }
		constexpr float getEyeAdaptionKey() const { return eyeadaptionKey; }
		constexpr float getEyeAdaptionRate() const { return eyeadaptionRate; }
		constexpr float getFSRSharpness() const { return fsrSharpness; }
		constexpr float getFSR2Sharpness() const { return fsr2Sharpness; }
		constexpr float getLightShaftsStrength() const { return lightShaftsStrength; }
		constexpr float getRaytracedDiffuseRange() const { return raytracedDiffuseRange; }
		constexpr float getRaytracedReflectionsRange() const { return raytracedReflectionsRange; }
		constexpr float getReflectionRoughnessCutoff() const { return reflectionRoughnessCutoff; }
		constexpr vz::renderer::Tonemap getTonemap() const { return tonemap; }

		constexpr bool getAOEnabled() const { return ao != AO_DISABLED; }
		constexpr AO getAO() const { return ao; }
		constexpr bool getSSREnabled() const { return ssrEnabled; }
		constexpr bool getRaytracedDiffuseEnabled() const { return raytracedDiffuseEnabled; }
		constexpr bool getRaytracedReflectionEnabled() const { return raytracedReflectionsEnabled; }
		constexpr bool getShadowsEnabled() const { return shadowsEnabled; }
		constexpr bool getReflectionsEnabled() const { return reflectionsEnabled; }
		constexpr bool getFXAAEnabled() const { return fxaaEnabled; }
		constexpr bool getBloomEnabled() const { return bloomEnabled; }
		constexpr bool getColorGradingEnabled() const { return colorGradingEnabled; }
		constexpr bool getVolumeLightsEnabled() const { return volumeLightsEnabled; }
		constexpr bool getLightShaftsEnabled() const { return lightShaftsEnabled; }
		constexpr bool getLensFlareEnabled() const { return lensFlareEnabled; }
		constexpr bool getMotionBlurEnabled() const { return motionBlurEnabled; }
		constexpr bool getDepthOfFieldEnabled() const { return depthOfFieldEnabled; }
		constexpr bool getEyeAdaptionEnabled() const { return eyeAdaptionEnabled; }
		constexpr bool getSharpenFilterEnabled() const { return sharpenFilterEnabled && getSharpenFilterAmount() > 0; }
		constexpr bool getOutlineEnabled() const { return outlineEnabled; }
		constexpr bool getChromaticAberrationEnabled() const { return chromaticAberrationEnabled; }
		constexpr bool getDitherEnabled() const { return ditherEnabled; }
		constexpr bool getOcclusionCullingEnabled() const { return occlusionCullingEnabled; }
		constexpr bool getSceneUpdateEnabled() const { return sceneUpdateEnabled; }
		constexpr bool getFSREnabled() const { return fsrEnabled; }
		constexpr bool getFSR2Enabled() const { return fsr2Enabled; }

		constexpr uint32_t getMSAASampleCount() const { return msaaSampleCount; }

		constexpr void setExposure(float value) { exposure = value; }
		constexpr void setBrightness(float value) { brightness = value; }
		constexpr void setContrast(float value) { contrast = value; }
		constexpr void setSaturation(float value) { saturation = value; }
		constexpr void setBloomThreshold(float value) { bloomThreshold = value; }
		constexpr void setMotionBlurStrength(float value) { motionBlurStrength = value; }
		constexpr void setDepthOfFieldStrength(float value) { dofStrength = value; }
		constexpr void setSharpenFilterAmount(float value) { sharpenFilterAmount = value; }
		constexpr void setOutlineThreshold(float value) { outlineThreshold = value; }
		constexpr void setOutlineThickness(float value) { outlineThickness = value; }
		constexpr void setOutlineColor(const XMFLOAT4& value) { outlineColor = value; }
		constexpr void setAORange(float value) { aoRange = value; }
		constexpr void setAOSampleCount(uint32_t value) { aoSampleCount = value; }
		constexpr void setAOPower(float value) { aoPower = value; }
		constexpr void setChromaticAberrationAmount(float value) { chromaticAberrationAmount = value; }
		constexpr void setScreenSpaceShadowSampleCount(uint32_t value) { screenSpaceShadowSampleCount = value; }
		constexpr void setScreenSpaceShadowRange(float value) { screenSpaceShadowRange = value; }
		constexpr void setEyeAdaptionKey(float value) { eyeadaptionKey = value; }
		constexpr void setEyeAdaptionRate(float value) { eyeadaptionRate = value; }
		constexpr void setFSRSharpness(float value) { fsrSharpness = value; }
		constexpr void setFSR2Sharpness(float value) { fsr2Sharpness = value; }
		constexpr void setLightShaftsStrength(float value) { lightShaftsStrength = value; }
		constexpr void setRaytracedDiffuseRange(float value) { raytracedDiffuseRange = value; }
		constexpr void setRaytracedReflectionsRange(float value) { raytracedReflectionsRange = value; }
		constexpr void setReflectionRoughnessCutoff(float value) { reflectionRoughnessCutoff = value; }
		constexpr void setTonemap(vz::renderer::Tonemap value) { tonemap = value; }

		void setAO(AO value);
		void setSSREnabled(bool value);
		void setRaytracedReflectionsEnabled(bool value);
		void setRaytracedDiffuseEnabled(bool value);
		void setMotionBlurEnabled(bool value);
		void setDepthOfFieldEnabled(bool value);
		void setEyeAdaptionEnabled(bool value);
		void setReflectionsEnabled(bool value);
		void setBloomEnabled(bool value);
		void setVolumeLightsEnabled(bool value);
		void setLightShaftsEnabled(bool value);
		void setOutlineEnabled(bool value);
		constexpr void setShadowsEnabled(bool value) { shadowsEnabled = value; }
		constexpr void setFXAAEnabled(bool value) { fxaaEnabled = value; }
		constexpr void setColorGradingEnabled(bool value) { colorGradingEnabled = value; }
		constexpr void setLensFlareEnabled(bool value) { lensFlareEnabled = value; }
		constexpr void setSharpenFilterEnabled(bool value) { sharpenFilterEnabled = value; }
		constexpr void setChromaticAberrationEnabled(bool value) { chromaticAberrationEnabled = value; }
		constexpr void setDitherEnabled(bool value) { ditherEnabled = value; }
		constexpr void setOcclusionCullingEnabled(bool value) { occlusionCullingEnabled = value; }
		constexpr void setSceneUpdateEnabled(bool value) { sceneUpdateEnabled = value; }
		void setFSREnabled(bool value);
		void setFSR2Enabled(bool value);
		void setFSR2Preset(FSR2_Preset preset); // this will modify resolution scaling and sampler lod bias

		virtual void setMSAASampleCount(uint32_t value) { msaaSampleCount = value; }

		struct CustomPostprocess
		{
			std::string name = "CustomPostprocess";
			vz::graphics::Shader computeshader;
			XMFLOAT4 params0;
			XMFLOAT4 params1;
			enum class Stage
			{
				BeforeTonemap, // Before tonemap and bloom in HDR color space
				AfterTonemap // After tonemap, in display color space
			} stage = Stage::AfterTonemap;
		};
		vz::vector<CustomPostprocess> custom_post_processes;

		void PreUpdate() override;
		void Update(float dt) override;
		void Render() const override;
		void Compose(vz::graphics::CommandList cmd) const override;

		void Stop() override;
		void Start() override;
	};

}