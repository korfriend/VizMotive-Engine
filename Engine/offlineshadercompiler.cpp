#include "vzEngine.h"

#include <iostream>
#include <iomanip>
#include <mutex>
#include <string>
#include <cstdlib>

std::mutex locker;
struct ShaderEntry
{
	std::string name;
	vz::graphics::ShaderStage stage = vz::graphics::ShaderStage::Count;
	vz::graphics::ShaderModel minshadermodel = vz::graphics::ShaderModel::SM_5_0;
	struct Permutation
	{
		vz::vector<std::string> defines;
	};
	vz::vector<Permutation> permutations;
};
vz::vector<ShaderEntry> shaders = {
	{"hairparticle_simulateCS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_simulateCS", vz::graphics::ShaderStage::CS},
	{"generateMIPChainCubeCS_float4", vz::graphics::ShaderStage::CS},
	{"generateMIPChainCubeCS_unorm4", vz::graphics::ShaderStage::CS},
	{"generateMIPChainCubeArrayCS_float4", vz::graphics::ShaderStage::CS},
	{"generateMIPChainCubeArrayCS_unorm4", vz::graphics::ShaderStage::CS},
	{"generateMIPChain3DCS_float4", vz::graphics::ShaderStage::CS},
	{"generateMIPChain3DCS_unorm4", vz::graphics::ShaderStage::CS},
	{"generateMIPChain2DCS_float4", vz::graphics::ShaderStage::CS},
	{"generateMIPChain2DCS_unorm4", vz::graphics::ShaderStage::CS},
	{"blockcompressCS_BC1", vz::graphics::ShaderStage::CS},
	{"blockcompressCS_BC3", vz::graphics::ShaderStage::CS},
	{"blockcompressCS_BC4", vz::graphics::ShaderStage::CS},
	{"blockcompressCS_BC5", vz::graphics::ShaderStage::CS},
	{"blockcompressCS_BC6H", vz::graphics::ShaderStage::CS},
	{"blockcompressCS_BC6H_cubemap", vz::graphics::ShaderStage::CS},
	{"blur_gaussian_float4CS", vz::graphics::ShaderStage::CS},
	{"bloomseparateCS", vz::graphics::ShaderStage::CS},
	{"depthoffield_mainCS", vz::graphics::ShaderStage::CS},
	{"depthoffield_neighborhoodMaxCOCCS", vz::graphics::ShaderStage::CS},
	{"depthoffield_prepassCS", vz::graphics::ShaderStage::CS},
	{"depthoffield_upsampleCS", vz::graphics::ShaderStage::CS},
	{"depthoffield_tileMaxCOC_verticalCS", vz::graphics::ShaderStage::CS},
	{"depthoffield_tileMaxCOC_horizontalCS", vz::graphics::ShaderStage::CS},
	{"vxgi_offsetprevCS", vz::graphics::ShaderStage::CS},
	{"vxgi_temporalCS", vz::graphics::ShaderStage::CS},
	{"vxgi_sdf_jumpfloodCS", vz::graphics::ShaderStage::CS},
	{"vxgi_resolve_diffuseCS", vz::graphics::ShaderStage::CS},
	{"vxgi_resolve_specularCS", vz::graphics::ShaderStage::CS},
	{"upsample_bilateral_float1CS", vz::graphics::ShaderStage::CS},
	{"upsample_bilateral_float4CS", vz::graphics::ShaderStage::CS},
	{"upsample_bilateral_unorm1CS", vz::graphics::ShaderStage::CS},
	{"upsample_bilateral_unorm4CS", vz::graphics::ShaderStage::CS},
	{"temporalaaCS", vz::graphics::ShaderStage::CS},
	{"tileFrustumsCS", vz::graphics::ShaderStage::CS},
	{"tonemapCS", vz::graphics::ShaderStage::CS},
	{"underwaterCS", vz::graphics::ShaderStage::CS},
	{"fsr_upscalingCS", vz::graphics::ShaderStage::CS},
	{"fsr_sharpenCS", vz::graphics::ShaderStage::CS},
	{"ffx-fsr2/ffx_fsr2_autogen_reactive_pass", vz::graphics::ShaderStage::CS},
	{"ffx-fsr2/ffx_fsr2_compute_luminance_pyramid_pass", vz::graphics::ShaderStage::CS},
	{"ffx-fsr2/ffx_fsr2_prepare_input_color_pass", vz::graphics::ShaderStage::CS},
	{"ffx-fsr2/ffx_fsr2_reconstruct_previous_depth_pass", vz::graphics::ShaderStage::CS},
	{"ffx-fsr2/ffx_fsr2_depth_clip_pass", vz::graphics::ShaderStage::CS},
	{"ffx-fsr2/ffx_fsr2_lock_pass", vz::graphics::ShaderStage::CS},
	{"ffx-fsr2/ffx_fsr2_accumulate_pass", vz::graphics::ShaderStage::CS},
	{"ffx-fsr2/ffx_fsr2_rcas_pass", vz::graphics::ShaderStage::CS},
	{"ssaoCS", vz::graphics::ShaderStage::CS},
	{"rtdiffuseCS", vz::graphics::ShaderStage::CS, vz::graphics::ShaderModel::SM_6_5},
	{"rtdiffuse_spatialCS", vz::graphics::ShaderStage::CS},
	{"rtdiffuse_temporalCS", vz::graphics::ShaderStage::CS},
	{"rtdiffuse_bilateralCS", vz::graphics::ShaderStage::CS},
	{"rtreflectionCS", vz::graphics::ShaderStage::CS, vz::graphics::ShaderModel::SM_6_5},
	{"ssr_tileMaxRoughness_horizontalCS", vz::graphics::ShaderStage::CS},
	{"ssr_tileMaxRoughness_verticalCS", vz::graphics::ShaderStage::CS},
	{"ssr_depthHierarchyCS", vz::graphics::ShaderStage::CS},
	{"ssr_resolveCS", vz::graphics::ShaderStage::CS},
	{"ssr_temporalCS", vz::graphics::ShaderStage::CS},
	{"ssr_bilateralCS", vz::graphics::ShaderStage::CS},
	{"ssr_raytraceCS", vz::graphics::ShaderStage::CS},
	{"ssr_raytraceCS_cheap", vz::graphics::ShaderStage::CS},
	{"ssr_raytraceCS_earlyexit", vz::graphics::ShaderStage::CS},
	{"sharpenCS", vz::graphics::ShaderStage::CS},
	{"skinningCS", vz::graphics::ShaderStage::CS},
	{"resolveMSAADepthStencilCS", vz::graphics::ShaderStage::CS},
	{"raytraceCS", vz::graphics::ShaderStage::CS},
	{"raytraceCS_rtapi", vz::graphics::ShaderStage::CS, vz::graphics::ShaderModel::SM_6_5},
	{"paint_textureCS", vz::graphics::ShaderStage::CS},
	{"oceanUpdateDisplacementMapCS", vz::graphics::ShaderStage::CS},
	{"oceanUpdateGradientFoldingCS", vz::graphics::ShaderStage::CS},
	{"oceanSimulatorCS", vz::graphics::ShaderStage::CS},
	{"msao_interleaveCS", vz::graphics::ShaderStage::CS},
	{"msao_preparedepthbuffers1CS", vz::graphics::ShaderStage::CS},
	{"msao_preparedepthbuffers2CS", vz::graphics::ShaderStage::CS},
	{"msao_blurupsampleCS", vz::graphics::ShaderStage::CS},
	{"msao_blurupsampleCS_blendout", vz::graphics::ShaderStage::CS},
	{"msao_blurupsampleCS_premin", vz::graphics::ShaderStage::CS},
	{"msao_blurupsampleCS_premin_blendout", vz::graphics::ShaderStage::CS},
	{"msaoCS", vz::graphics::ShaderStage::CS},
	{"motionblur_neighborhoodMaxVelocityCS", vz::graphics::ShaderStage::CS},
	{"motionblur_tileMaxVelocity_horizontalCS", vz::graphics::ShaderStage::CS},
	{"motionblur_tileMaxVelocity_verticalCS", vz::graphics::ShaderStage::CS},
	{"luminancePass2CS", vz::graphics::ShaderStage::CS},
	{"motionblurCS", vz::graphics::ShaderStage::CS},
	{"motionblurCS_cheap", vz::graphics::ShaderStage::CS},
	{"motionblurCS_earlyexit", vz::graphics::ShaderStage::CS},
	{"luminancePass1CS", vz::graphics::ShaderStage::CS},
	{"lightShaftsCS", vz::graphics::ShaderStage::CS},
	{"lightCullingCS_ADVANCED_DEBUG", vz::graphics::ShaderStage::CS},
	{"lightCullingCS_DEBUG", vz::graphics::ShaderStage::CS},
	{"lightCullingCS", vz::graphics::ShaderStage::CS},
	{"lightCullingCS_ADVANCED", vz::graphics::ShaderStage::CS},
	{"hbaoCS", vz::graphics::ShaderStage::CS},
	{"gpusortlib_sortInnerCS", vz::graphics::ShaderStage::CS},
	{"gpusortlib_sortStepCS", vz::graphics::ShaderStage::CS},
	{"gpusortlib_kickoffSortCS", vz::graphics::ShaderStage::CS},
	{"gpusortlib_sortCS", vz::graphics::ShaderStage::CS},
	{"fxaaCS", vz::graphics::ShaderStage::CS},
	{"filterEnvMapCS", vz::graphics::ShaderStage::CS},
	{"fft_512x512_c2c_CS", vz::graphics::ShaderStage::CS},
	{"fft_512x512_c2c_v2_CS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_sphpartitionCS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_sphcellallocationCS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_sphbinningCS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_simulateCS_SORTING", vz::graphics::ShaderStage::CS},
	{"emittedparticle_simulateCS_SORTING_DEPTHCOLLISIONS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_sphdensityCS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_sphforceCS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_kickoffUpdateCS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_simulateCS_DEPTHCOLLISIONS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_emitCS", vz::graphics::ShaderStage::CS},
	{"emittedparticle_emitCS_FROMMESH", vz::graphics::ShaderStage::CS},
	{"emittedparticle_emitCS_volume", vz::graphics::ShaderStage::CS},
	{"emittedparticle_finishUpdateCS", vz::graphics::ShaderStage::CS},
	{"downsample4xCS", vz::graphics::ShaderStage::CS},
	{"lineardepthCS", vz::graphics::ShaderStage::CS},
	{"depthoffield_prepassCS_earlyexit", vz::graphics::ShaderStage::CS},
	{"depthoffield_mainCS_cheap", vz::graphics::ShaderStage::CS},
	{"depthoffield_mainCS_earlyexit", vz::graphics::ShaderStage::CS },
	{"depthoffield_postfilterCS", vz::graphics::ShaderStage::CS },
	{"copytexture2D_float4_borderexpandCS", vz::graphics::ShaderStage::CS },
	{"copytexture2D_unorm4_borderexpandCS", vz::graphics::ShaderStage::CS },
	{"copytexture2D_unorm4CS", vz::graphics::ShaderStage::CS },
	{"copytexture2D_float4CS", vz::graphics::ShaderStage::CS },
	{"chromatic_aberrationCS", vz::graphics::ShaderStage::CS },
	{"bvh_hierarchyCS", vz::graphics::ShaderStage::CS },
	{"bvh_primitivesCS", vz::graphics::ShaderStage::CS },
	{"bvh_propagateaabbCS", vz::graphics::ShaderStage::CS },
	{"blur_gaussian_wide_unorm1CS", vz::graphics::ShaderStage::CS },
	{"blur_gaussian_wide_unorm4CS", vz::graphics::ShaderStage::CS },
	{"blur_gaussian_unorm1CS", vz::graphics::ShaderStage::CS },
	{"blur_gaussian_unorm4CS", vz::graphics::ShaderStage::CS },
	{"blur_gaussian_wide_float1CS", vz::graphics::ShaderStage::CS },
	{"blur_gaussian_wide_float3CS", vz::graphics::ShaderStage::CS },
	{"blur_gaussian_wide_float4CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_wide_unorm4CS", vz::graphics::ShaderStage::CS },
	{"blur_gaussian_float1CS", vz::graphics::ShaderStage::CS },
	{"blur_gaussian_float3CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_unorm4CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_wide_float1CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_wide_float3CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_wide_float4CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_wide_unorm1CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_float1CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_float3CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_float4CS", vz::graphics::ShaderStage::CS },
	{"blur_bilateral_unorm1CS", vz::graphics::ShaderStage::CS },
	{"normalsfromdepthCS", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_curlnoiseCS", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_detailnoiseCS", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_renderCS", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_renderCS_capture", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_renderCS_capture_MSAA", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_reprojectCS", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_shadow_filterCS", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_shadow_renderCS", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_shapenoiseCS", vz::graphics::ShaderStage::CS },
	{"volumetricCloud_upsamplePS", vz::graphics::ShaderStage::PS },
	{"volumetricCloud_weathermapCS", vz::graphics::ShaderStage::CS },
	{"shadingRateClassificationCS", vz::graphics::ShaderStage::CS },
	{"shadingRateClassificationCS_DEBUG", vz::graphics::ShaderStage::CS },
	{"aerialPerspectiveCS", vz::graphics::ShaderStage::CS },
	{"aerialPerspectiveCS_capture", vz::graphics::ShaderStage::CS },
	{"aerialPerspectiveCS_capture_MSAA", vz::graphics::ShaderStage::CS },
	{"skyAtmosphere_cameraVolumeLutCS", vz::graphics::ShaderStage::CS },
	{"skyAtmosphere_transmittanceLutCS", vz::graphics::ShaderStage::CS },
	{"skyAtmosphere_skyViewLutCS", vz::graphics::ShaderStage::CS },
	{"skyAtmosphere_multiScatteredLuminanceLutCS", vz::graphics::ShaderStage::CS },
	{"skyAtmosphere_skyLuminanceLutCS", vz::graphics::ShaderStage::CS },
	{"upsample_bilateral_uint4CS", vz::graphics::ShaderStage::CS },
	{"screenspaceshadowCS", vz::graphics::ShaderStage::CS },
	{"rtshadowCS", vz::graphics::ShaderStage::CS, vz::graphics::ShaderModel::SM_6_5 },
	{"rtshadow_denoise_tileclassificationCS", vz::graphics::ShaderStage::CS },
	{"rtshadow_denoise_filterCS", vz::graphics::ShaderStage::CS },
	{"rtshadow_denoise_temporalCS", vz::graphics::ShaderStage::CS },
	{"rtaoCS", vz::graphics::ShaderStage::CS, vz::graphics::ShaderModel::SM_6_5 },
	{"rtao_denoise_tileclassificationCS", vz::graphics::ShaderStage::CS },
	{"rtao_denoise_filterCS", vz::graphics::ShaderStage::CS },
	{"visibility_resolveCS", vz::graphics::ShaderStage::CS },
	{"visibility_resolveCS_MSAA", vz::graphics::ShaderStage::CS },
	{"visibility_velocityCS", vz::graphics::ShaderStage::CS },
	{"visibility_skyCS", vz::graphics::ShaderStage::CS },
	{"surfel_coverageCS", vz::graphics::ShaderStage::CS },
	{"surfel_indirectprepareCS", vz::graphics::ShaderStage::CS },
	{"surfel_updateCS", vz::graphics::ShaderStage::CS },
	{"surfel_gridoffsetsCS", vz::graphics::ShaderStage::CS },
	{"surfel_binningCS", vz::graphics::ShaderStage::CS },
	{"surfel_raytraceCS_rtapi", vz::graphics::ShaderStage::CS, vz::graphics::ShaderModel::SM_6_5 },
	{"surfel_raytraceCS", vz::graphics::ShaderStage::CS },
	{"surfel_integrateCS", vz::graphics::ShaderStage::CS },
	{"ddgi_raytraceCS", vz::graphics::ShaderStage::CS },
	{"ddgi_raytraceCS_rtapi", vz::graphics::ShaderStage::CS, vz::graphics::ShaderModel::SM_6_5 },
	{"ddgi_updateCS", vz::graphics::ShaderStage::CS },
	{"ddgi_updateCS_depth", vz::graphics::ShaderStage::CS },
	{"terrainVirtualTextureUpdateCS", vz::graphics::ShaderStage::CS },
	{"terrainVirtualTextureUpdateCS_normalmap", vz::graphics::ShaderStage::CS },
	{"terrainVirtualTextureUpdateCS_surfacemap", vz::graphics::ShaderStage::CS },
	{"meshlet_prepareCS", vz::graphics::ShaderStage::CS },
	{"impostor_prepareCS", vz::graphics::ShaderStage::CS },
	{"virtualTextureTileRequestsCS", vz::graphics::ShaderStage::CS },
	{"virtualTextureTileAllocateCS", vz::graphics::ShaderStage::CS },
	{"virtualTextureResidencyUpdateCS", vz::graphics::ShaderStage::CS },
	{"vzndCS", vz::graphics::ShaderStage::CS },
	{"yuv_to_rgbCS", vz::graphics::ShaderStage::CS },


	{"emittedparticlePS_soft", vz::graphics::ShaderStage::PS },
	{"imagePS", vz::graphics::ShaderStage::PS },
	{"emittedparticlePS_soft_lighting", vz::graphics::ShaderStage::PS },
	{"oceanSurfacePS", vz::graphics::ShaderStage::PS },
	{"hairparticlePS", vz::graphics::ShaderStage::PS },
	{"hairparticlePS_simple", vz::graphics::ShaderStage::PS },
	{"hairparticlePS_prepass", vz::graphics::ShaderStage::PS },
	{"hairparticlePS_prepass_depthonly", vz::graphics::ShaderStage::PS },
	{"hairparticlePS_shadow", vz::graphics::ShaderStage::PS },
	{"volumetricLight_SpotPS", vz::graphics::ShaderStage::PS },
	{"volumetricLight_PointPS", vz::graphics::ShaderStage::PS },
	{"volumetricLight_DirectionalPS", vz::graphics::ShaderStage::PS },
	{"voxelPS", vz::graphics::ShaderStage::PS },
	{"vertexcolorPS", vz::graphics::ShaderStage::PS },
	{"upsample_bilateralPS", vz::graphics::ShaderStage::PS },
	{"sunPS", vz::graphics::ShaderStage::PS },
	{"skyPS_dynamic", vz::graphics::ShaderStage::PS },
	{"skyPS_static", vz::graphics::ShaderStage::PS },
	{"shadowPS_transparent", vz::graphics::ShaderStage::PS },
	{"shadowPS_water", vz::graphics::ShaderStage::PS },
	{"shadowPS_alphatest", vz::graphics::ShaderStage::PS },
	{"renderlightmapPS", vz::graphics::ShaderStage::PS },
	{"renderlightmapPS_rtapi", vz::graphics::ShaderStage::PS, vz::graphics::ShaderModel::SM_6_5 },
	{"raytrace_debugbvhPS", vz::graphics::ShaderStage::PS },
	{"outlinePS", vz::graphics::ShaderStage::PS },
	{"oceanSurfaceSimplePS", vz::graphics::ShaderStage::PS },
	{"objectPS_voxelizer", vz::graphics::ShaderStage::PS },
	{"objectPS_hologram", vz::graphics::ShaderStage::PS },
	{"objectPS_paintradius", vz::graphics::ShaderStage::PS },
	{"objectPS_simple", vz::graphics::ShaderStage::PS },
	{"objectPS_debug", vz::graphics::ShaderStage::PS },
	{"objectPS_prepass", vz::graphics::ShaderStage::PS },
	{"objectPS_prepass_alphatest", vz::graphics::ShaderStage::PS },
	{"objectPS_prepass_depthonly", vz::graphics::ShaderStage::PS },
	{"objectPS_prepass_depthonly_alphatest", vz::graphics::ShaderStage::PS },
	{"lightVisualizerPS", vz::graphics::ShaderStage::PS },
	{"lensFlarePS", vz::graphics::ShaderStage::PS },
	{"impostorPS", vz::graphics::ShaderStage::PS },
	{"impostorPS_simple", vz::graphics::ShaderStage::PS },
	{"impostorPS_prepass", vz::graphics::ShaderStage::PS },
	{"impostorPS_prepass_depthonly", vz::graphics::ShaderStage::PS },
	{"forceFieldVisualizerPS", vz::graphics::ShaderStage::PS },
	{"fontPS", vz::graphics::ShaderStage::PS },
	{"envMap_skyPS_static", vz::graphics::ShaderStage::PS },
	{"envMap_skyPS_dynamic", vz::graphics::ShaderStage::PS },
	{"envMapPS", vz::graphics::ShaderStage::PS },
	{"emittedparticlePS_soft_distortion", vz::graphics::ShaderStage::PS },
	{"downsampleDepthBuffer4xPS", vz::graphics::ShaderStage::PS },
	{"emittedparticlePS_simple", vz::graphics::ShaderStage::PS },
	{"cubeMapPS", vz::graphics::ShaderStage::PS },
	{"circlePS", vz::graphics::ShaderStage::PS },
	{"captureImpostorPS", vz::graphics::ShaderStage::PS },
	{"ddgi_debugPS", vz::graphics::ShaderStage::PS },
	{"copyDepthPS", vz::graphics::ShaderStage::PS },
	{"copyStencilBitPS", vz::graphics::ShaderStage::PS },


	{"hairparticleVS", vz::graphics::ShaderStage::VS },
	{"emittedparticleVS", vz::graphics::ShaderStage::VS },
	{"imageVS", vz::graphics::ShaderStage::VS },
	{"fontVS", vz::graphics::ShaderStage::VS },
	{"voxelVS", vz::graphics::ShaderStage::VS },
	{"vertexcolorVS", vz::graphics::ShaderStage::VS },
	{"volumetriclight_directionalVS", vz::graphics::ShaderStage::VS },
	{"volumetriclight_pointVS", vz::graphics::ShaderStage::VS },
	{"volumetriclight_spotVS", vz::graphics::ShaderStage::VS },
	{"vSpotLightVS", vz::graphics::ShaderStage::VS },
	{"vPointLightVS", vz::graphics::ShaderStage::VS },
	{"sphereVS", vz::graphics::ShaderStage::VS },
	{"skyVS", vz::graphics::ShaderStage::VS },
	{"postprocessVS", vz::graphics::ShaderStage::VS },
	{"renderlightmapVS", vz::graphics::ShaderStage::VS },
	{"raytrace_screenVS", vz::graphics::ShaderStage::VS },
	{"oceanSurfaceVS", vz::graphics::ShaderStage::VS },
	{"objectVS_debug", vz::graphics::ShaderStage::VS },
	{"objectVS_voxelizer", vz::graphics::ShaderStage::VS },
	{"lensFlareVS", vz::graphics::ShaderStage::VS },
	{"impostorVS", vz::graphics::ShaderStage::VS },
	{"forceFieldPointVisualizerVS", vz::graphics::ShaderStage::VS },
	{"forceFieldPlaneVisualizerVS", vz::graphics::ShaderStage::VS },
	{"envMap_skyVS", vz::graphics::ShaderStage::VS },
	{"envMapVS", vz::graphics::ShaderStage::VS },
	{"envMap_skyVS_emulation", vz::graphics::ShaderStage::VS },
	{"envMapVS_emulation", vz::graphics::ShaderStage::VS },
	{"occludeeVS", vz::graphics::ShaderStage::VS },
	{"ddgi_debugVS", vz::graphics::ShaderStage::VS },
	{"envMap_skyGS_emulation", vz::graphics::ShaderStage::GS },
	{"envMapGS_emulation", vz::graphics::ShaderStage::GS },
	{"shadowGS_emulation", vz::graphics::ShaderStage::GS },
	{"shadowGS_alphatest_emulation", vz::graphics::ShaderStage::GS },
	{"shadowGS_transparent_emulation", vz::graphics::ShaderStage::GS },
	{"objectGS_primitiveID_emulation", vz::graphics::ShaderStage::GS },
	{"objectGS_primitiveID_emulation_alphatest", vz::graphics::ShaderStage::GS },
	{"voxelGS", vz::graphics::ShaderStage::GS },
	{"objectGS_voxelizer", vz::graphics::ShaderStage::GS },
	{"objectVS_simple", vz::graphics::ShaderStage::VS },
	{"objectVS_common", vz::graphics::ShaderStage::VS },
	{"objectVS_common_tessellation", vz::graphics::ShaderStage::VS },
	{"objectVS_prepass", vz::graphics::ShaderStage::VS },
	{"objectVS_prepass_alphatest", vz::graphics::ShaderStage::VS },
	{"objectVS_prepass_tessellation", vz::graphics::ShaderStage::VS },
	{"objectVS_prepass_alphatest_tessellation", vz::graphics::ShaderStage::VS },
	{"objectVS_simple_tessellation", vz::graphics::ShaderStage::VS },
	{"shadowVS", vz::graphics::ShaderStage::VS },
	{"shadowVS_alphatest", vz::graphics::ShaderStage::VS },
	{"shadowVS_emulation", vz::graphics::ShaderStage::VS },
	{"shadowVS_alphatest_emulation", vz::graphics::ShaderStage::VS },
	{"shadowVS_transparent", vz::graphics::ShaderStage::VS },
	{"shadowVS_transparent_emulation", vz::graphics::ShaderStage::VS },
	{"screenVS", vz::graphics::ShaderStage::VS },



	{"objectDS", vz::graphics::ShaderStage::DS },
	{"objectDS_prepass", vz::graphics::ShaderStage::DS },
	{"objectDS_prepass_alphatest", vz::graphics::ShaderStage::DS },
	{"objectDS_simple", vz::graphics::ShaderStage::DS },


	{"objectHS", vz::graphics::ShaderStage::HS },
	{"objectHS_prepass", vz::graphics::ShaderStage::HS },
	{"objectHS_prepass_alphatest", vz::graphics::ShaderStage::HS },
	{"objectHS_simple", vz::graphics::ShaderStage::HS },

	{"emittedparticleMS", vz::graphics::ShaderStage::MS },


	//{"rtreflectionLIB", vz::graphics::ShaderStage::LIB },
};

struct Target
{
	vz::graphics::ShaderFormat format;
	std::string dir;
};
vz::vector<Target> targets;
vz::unordered_map<std::string, vz::shadercompiler::CompilerOutput> results;
bool rebuild = false;
bool shaderdump_enabled = false;

using namespace vz::graphics;

int main(int argc, char* argv[])
{
	vz::shadercompiler::Flags compile_flags = vz::shadercompiler::Flags::NONE;
	std::cout << "[Wicked Engine Offline Shader Compiler]\n";
	std::cout << "Available command arguments:\n";
	std::cout << "\thlsl5 : \t\tCompile shaders to hlsl5 (dx11) format (using d3dcompiler)\n";
	std::cout << "\thlsl6 : \t\tCompile shaders to hlsl6 (dx12) format (using dxcompiler)\n";
	std::cout << "\tspirv : \t\tCompile shaders to spirv (vulkan) format (using dxcompiler)\n";
	std::cout << "\thlsl6_xs : \t\tCompile shaders to hlsl6 Xbox Series native (dx12) format (requires Xbox SDK)\n";
	std::cout << "\tps5 : \t\t\tCompile shaders to PlayStation 5 native format (requires PlayStation 5 SDK)\n";
	std::cout << "\trebuild : \t\tAll shaders will be rebuilt, regardless if they are outdated or not\n";
	std::cout << "\tdisable_optimization : \tShaders will be compiled without optimizations\n";
	std::cout << "\tstrip_reflection : \tReflection will be stripped from shader binary to reduce file size\n";
	std::cout << "\tshaderdump : \t\tShaders will be saved to wiShaderDump.h C++ header file (rebuild is assumed)\n";
	std::cout << "Command arguments used: ";

	vz::arguments::Parse(argc, argv);

	if (vz::arguments::HasArgument("hlsl5"))
	{
		targets.push_back({ ShaderFormat::HLSL5, "shaders/hlsl5/" });
		std::cout << "hlsl5 ";
	}
	if (vz::arguments::HasArgument("hlsl6"))
	{
		targets.push_back({ ShaderFormat::HLSL6, "shaders/hlsl6/" });
		std::cout << "hlsl6 ";
	}
	if (vz::arguments::HasArgument("spirv"))
	{
		targets.push_back({ ShaderFormat::SPIRV, "shaders/spirv/" });
		std::cout << "spirv ";
	}
	if (vz::arguments::HasArgument("hlsl6_xs"))
	{
		targets.push_back({ ShaderFormat::HLSL6_XS, "shaders/hlsl6_xs/" });
		std::cout << "hlsl6_xs ";
	}
	if (vz::arguments::HasArgument("ps5"))
	{
		targets.push_back({ ShaderFormat::PS5, "shaders/ps5/" });
		std::cout << "ps5 ";
	}

	if (vz::arguments::HasArgument("shaderdump"))
	{
		shaderdump_enabled = true;
		rebuild = true;
		std::cout << "shaderdump ";
	}

	if (vz::arguments::HasArgument("rebuild"))
	{
		rebuild = true;
		std::cout << "rebuild ";
	}

	if (vz::arguments::HasArgument("disable_optimization"))
	{
		compile_flags |= vz::shadercompiler::Flags::DISABLE_OPTIMIZATION;
		std::cout << "disable_optimization ";
	}

	if (vz::arguments::HasArgument("strip_reflection"))
	{
		compile_flags |= vz::shadercompiler::Flags::STRIP_REFLECTION;
		std::cout << "strip_reflection ";
	}

	std::cout << "\n";

	if (targets.empty())
	{
		targets = {
			//{ ShaderFormat::HLSL5, "shaders/hlsl5/" },
			{ ShaderFormat::HLSL6, "shaders/hlsl6/" },
			{ ShaderFormat::SPIRV, "shaders/spirv/" },
		};
		std::cout << "No shader formats were specified, assuming command arguments: spirv hlsl6\n";
	}

	// permutations for objectPS:
	shaders.push_back({ "objectPS", vz::graphics::ShaderStage::PS });
	for (auto& x : vz::scene::MaterialComponent::shaderTypeDefines)
	{
		shaders.back().permutations.emplace_back().defines = x;

		// same but with TRANSPARENT:
		shaders.back().permutations.emplace_back().defines = x;
		shaders.back().permutations.back().defines.push_back("TRANSPARENT");
	}

	// permutations for visibility_surfaceCS:
	shaders.push_back({ "visibility_surfaceCS", vz::graphics::ShaderStage::CS });
	for (auto& x : vz::scene::MaterialComponent::shaderTypeDefines)
	{
		shaders.back().permutations.emplace_back().defines = x;
	}

	// permutations for visibility_surfaceCS REDUCED:
	shaders.push_back({ "visibility_surfaceCS", vz::graphics::ShaderStage::CS });
	for (auto& x : vz::scene::MaterialComponent::shaderTypeDefines)
	{
		auto defines = x;
		defines.push_back("REDUCED");
		shaders.back().permutations.emplace_back().defines = defines;
	}

	// permutations for visibility_shadeCS:
	shaders.push_back({ "visibility_shadeCS", vz::graphics::ShaderStage::CS });
	for (auto& x : vz::scene::MaterialComponent::shaderTypeDefines)
	{
		shaders.back().permutations.emplace_back().defines = x;
	}

	vz::jobsystem::Initialize();
	vz::jobsystem::context ctx;

	std::string SHADERSOURCEPATH = vz::renderer::GetShaderSourcePath();
	vz::helper::MakePathAbsolute(SHADERSOURCEPATH);

	std::cout << "[Wicked Engine Offline Shader Compiler] Searching for outdated shaders...\n";
	vz::Timer timer;
	static int errors = 0;

	for (auto& target : targets)
	{
		std::string SHADERPATH = target.dir;
		vz::helper::DirectoryCreate(SHADERPATH);

		for (auto& shader : shaders)
		{
			if (target.format == ShaderFormat::HLSL5)
			{
				if (
					shader.stage == ShaderStage::MS ||
					shader.stage == ShaderStage::AS ||
					shader.stage == ShaderStage::LIB
					)
				{
					// shader stage not applicable to HLSL5
					continue;
				}
			}
			vz::vector<ShaderEntry::Permutation> permutations = shader.permutations;
			if (permutations.empty())
			{
				permutations.emplace_back();
			}

			for (auto permutation : permutations)
			{
				vz::jobsystem::Execute(ctx, [=](vz::jobsystem::JobArgs args) {
					std::string shaderbinaryfilename = SHADERPATH + shader.name;
					for (auto& def : permutation.defines)
					{
						shaderbinaryfilename += "_" + def;
					}
					shaderbinaryfilename += ".cso";
					if (!rebuild && !vz::shadercompiler::IsShaderOutdated(shaderbinaryfilename))
					{
						return;
					}

					vz::shadercompiler::CompilerInput input;
					input.flags = compile_flags;
					input.format = target.format;
					input.stage = shader.stage;
					input.shadersourcefilename = SHADERSOURCEPATH + shader.name + ".hlsl";
					input.include_directories.push_back(SHADERSOURCEPATH);
					input.include_directories.push_back(SHADERSOURCEPATH + vz::helper::GetDirectoryFromPath(shader.name));
					input.minshadermodel = shader.minshadermodel;
					input.defines = permutation.defines;

					if (input.minshadermodel > ShaderModel::SM_5_0 && target.format == ShaderFormat::HLSL5)
					{
						// if shader format cannot support shader model, then we cancel the task without returning error
						return;
					}
					if (target.format == ShaderFormat::PS5 && (input.minshadermodel >= ShaderModel::SM_6_5 || input.stage == ShaderStage::MS))
					{
						// TODO PS5 raytracing, mesh shader
						return;
					}

					vz::shadercompiler::CompilerOutput output;
					vz::shadercompiler::Compile(input, output);

					if (output.IsValid())
					{
						vz::shadercompiler::SaveShaderAndMetadata(shaderbinaryfilename, output);

						locker.lock();
						if (!output.error_message.empty())
						{
							std::cerr << output.error_message << "\n";
						}
						std::cout << "shader compiled: " << shaderbinaryfilename << "\n";
						if (shaderdump_enabled)
						{
							results[shaderbinaryfilename] = output;
						}
						locker.unlock();
					}
					else
					{
						locker.lock();
						std::cerr << "shader compile FAILED: " << shaderbinaryfilename << "\n" << output.error_message;
						errors++;
						locker.unlock();
					}

				});
			}
		}
	}
	vz::jobsystem::Wait(ctx);

	std::cout << "[Wicked Engine Offline Shader Compiler] Finished in " << std::setprecision(4) << timer.elapsed_seconds() << " seconds with " << errors << " errors\n";

	if (shaderdump_enabled)
	{
		std::cout << "[Wicked Engine Offline Shader Compiler] Creating ShaderDump...\n";
		timer.record();
		std::string ss;
		ss += "namespace vzShaderDump {\n";
		for (auto& x : results)
		{
			auto& name = x.first;
			auto& output = x.second;

			std::string name_repl = name;
			std::replace(name_repl.begin(), name_repl.end(), '/', '_');
			std::replace(name_repl.begin(), name_repl.end(), '.', '_');
			std::replace(name_repl.begin(), name_repl.end(), '-', '_');
			ss += "const uint8_t " + name_repl + "[] = {";
			for (size_t i = 0; i < output.shadersize; ++i)
			{
				ss += std::to_string((uint32_t)output.shaderdata[i]) + ",";
			}
			ss += "};\n";
		}
		ss += "struct ShaderDumpEntry{const uint8_t* data; size_t size;};\n";
		ss += "const vz::unordered_map<std::string, ShaderDumpEntry> shaderdump = {\n";
		for (auto& x : results)
		{
			auto& name = x.first;
			auto& output = x.second;

			std::string name_repl = name;
			std::replace(name_repl.begin(), name_repl.end(), '/', '_');
			std::replace(name_repl.begin(), name_repl.end(), '.', '_');
			std::replace(name_repl.begin(), name_repl.end(), '-', '_');
			ss += "{\"" + name + "\", {" + name_repl + ",sizeof(" + name_repl + ")}},\n";
		}
		ss += "};\n"; // map end
		ss += "}\n"; // namespace end
		vz::helper::FileWrite("vzShaderDump.h", (uint8_t*)ss.c_str(), ss.length());
		std::cout << "[Wicked Engine Offline Shader Compiler] ShaderDump written to wiShaderDump.h in " << std::setprecision(4) << timer.elapsed_seconds() << " seconds\n";
	}

	vz::jobsystem::ShutDown();

	return errors;
}
