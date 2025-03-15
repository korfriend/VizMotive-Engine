dxc -T vs_6_0 ./VS/meshVS_debug.hlsl -spirv
dxc -T vs_6_0 ./VS/meshVS_common.hlsl -spirv
dxc -T vs_6_0 ./VS/meshVS_simple.hlsl -spirv
dxc -T vs_6_0 ./VS/vertexcolorVS.hlsl -spirv
dxc -T vs_6_0 ./VS/occludeeVS.hlsl -spirv
dxc -T vs_6_0 ./VS/imageVS.hlsl -spirv
dxc -T vs_6_0 ./VS/meshVS_prepass.hlsl -spirv
dxc -T vs_6_0 ./VS/meshVS_prepass_alphatest.hlsl -spirv
dxc -T vs_6_0 ./VS/meshVS_prepass_alphatest_tessellation.hlsl -spirv
dxc -T vs_6_0 ./VS/meshVS_prepass_tessellation.hlsl -spirv

dxc -T ps_6_0 ./PS/meshPS_debug.hlsl -spirv
dxc -T ps_6_0 ./PS/meshPS_simple.hlsl -spirv
dxc -T ps_6_0 ./PS/meshPS.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T ps_6_0 ./PS/meshPS.hlsl -spirv /D TRANSPARENT=1 -fspv-target-env=vulkan1.1

dxc -T ps_6_0 ./PS/vertexcolorPS.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T ps_6_0 ./PS/meshPS_prepass.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T ps_6_0 ./PS/meshPS_prepass_alphatest.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T ps_6_0 ./PS/meshPS_prepass_depthonly.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T ps_6_0 ./PS/meshPS_prepass_depthonly_alphatest.hlsl -spirv -fspv-target-env=vulkan1.1

dxc -T ps_6_0 ./PS/imagePS.hlsl -spirv
dxc -T ps_6_0 ./PS/debugPS.hlsl -spirv

dxc -T cs_6_0 ./CS/view_resolveCS.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T cs_6_0 ./CS/lightCullingCS.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T cs_6_0 ./CS/lightCullingCS_ADVANCED.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T cs_6_0 ./CS/lightCullingCS_ADVANCED_DEBUG.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T cs_6_0 ./CS/lightCullingCS_DEBUG.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T cs_6_0 ./CS/generateMIPChainCubeArrayCS_float4.hlsl -spirv 
dxc -T cs_6_0 ./CS/generateMIPChainCubeArrayCS_unorm4.hlsl -spirv 
dxc -T cs_6_0 ./CS/generateMIPChainCubeCS_float4.hlsl -spirv 
dxc -T cs_6_0 ./CS/generateMIPChainCubeCS_unorm4.hlsl -spirv 
dxc -T cs_6_0 ./CS/generateMIPChain2DCS_float4.hlsl -spirv 
dxc -T cs_6_0 ./CS/generateMIPChain2DCS_unorm4.hlsl -spirv 
dxc -T cs_6_0 ./CS/generateMIPChain3DCS_float4.hlsl -spirv 
dxc -T cs_6_0 ./CS/generateMIPChain3DCS_unorm4.hlsl -spirv 
dxc -T cs_6_0 ./CS/blur_gaussian_float1CS.hlsl -spirv 
dxc -T cs_6_0 ./CS/blur_gaussian_float3CS.hlsl -spirv 
dxc -T cs_6_0 ./CS/blur_gaussian_float4CS.hlsl -spirv 
dxc -T cs_6_0 ./CS/blur_gaussian_unorm1CS.hlsl -spirv 
dxc -T cs_6_0 ./CS/blur_gaussian_unorm4CS.hlsl -spirv 
dxc -T cs_6_0 ./CS/blur_gaussian_wide_float1CS.hlsl -spirv 
dxc -T cs_6_0 ./CS/blur_gaussian_wide_float3CS.hlsl -spirv 
dxc -T cs_6_0 ./CS/blur_gaussian_wide_unorm1CS.hlsl -spirv 
dxc -T cs_6_0 ./CS/blur_gaussian_wide_unorm4CS.hlsl -spirv 
dxc -T cs_6_0 ./CS/blockcompressCS_BC1.hlsl -spirv
dxc -T cs_6_0 ./CS/blockcompressCS_BC3.hlsl -spirv
dxc -T cs_6_0 ./CS/blockcompressCS_BC4.hlsl -spirv
dxc -T cs_6_0 ./CS/blockcompressCS_BC5.hlsl -spirv
dxc -T cs_6_0 ./CS/blockcompressCS_BC6H.hlsl -spirv
dxc -T cs_6_0 ./CS/blockcompressCS_BC6H_cubemap.hlsl -spirv

dxc -T cs_6_0 ./CS/bvh_hierarchyCS.hlsl -spirv
dxc -T cs_6_0 ./CS/bvh_primitivesCS.hlsl -spirv
dxc -T cs_6_0 ./CS/bvh_primitivesCS_geometryspace.hlsl -spirv
dxc -T cs_6_0 ./CS/bvh_propagateaabbCS.hlsl -spirv

dxc -T cs_6_0 ./CS/wetmap_updateCS.hlsl -spirv
dxc -T cs_6_0 ./CS/meshlet_prepareCS.hlsl -spirv
dxc -T cs_6_0 ./CS/tonemapCS.hlsl -spirv

dxc -T cs_6_0 ./CS/gsplat_preprocessCS.hlsl -spirv
dxc -T cs_6_0 ./CS/gsplat_offsetCS.hlsl -spirv
dxc -T cs_6_0 ./CS/gsplat_duplicateWithKeysCS.hlsl -spirv
dxc -T cs_6_0 ./CS/gsplat_identifyTileRangesCS.hlsl -spirv
dxc -T cs_6_6 ./CS/gsplat_histCS.hlsl -spirv
dxc -T cs_6_6 ./CS/gsplat_sortCS.hlsl -spirv
dxc -T cs_6_0 ./CS/gsplat_renderCS.hlsl -spirv

dxc -T cs_6_0 ./CS/meshSlicerCS.hlsl -spirv
dxc -T cs_6_0 ./CS/meshSlicerCS_curvedplane.hlsl -spirv
dxc -T cs_6_0 ./CS/slicerOutlineCS.hlsl -spirv
dxc -T cs_6_0 ./CS/slicerResolveCS_KB2.hlsl -spirv

dxc -T gs_6_0 ./GS/thicknessLineGS.hlsl -spirv

dxc -T cs_6_0 ./CS/dvrCS_curved_slicer_2KB.hlsl -spirv
dxc -T cs_6_0 ./CS/dvrCS_slicer_2KB.hlsl -spirv
dxc -T cs_6_0 ./CS/dvrCS_woKB.hlsl -spirv
dxc -T cs_6_0 ./CS/dvrCS_zerothick_slicer.hlsl -spirv
dxc -T cs_6_0 ./CS/dvrCS_curved_slicer_nothickness.hlsl -spirv
dxc -T cs_6_0 ./CS/dvrCS_woKB_xray.hlsl -spirv

dxc -T cs_6_0 ./CS/dvrCS_curved_slicer_xray_2KB.hlsl -spirv
dxc -T cs_6_0 ./CS/dvrCS_slicer_xray_2KB.hlsl -spirv

dxc -T cs_6_0 ./CS/temporalaaCS.hlsl -spirv