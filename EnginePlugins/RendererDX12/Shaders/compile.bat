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

dxc -T cs_6_0 ./CS/view_resolveCS.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T cs_6_0 ./CS/lightCullingCS.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T cs_6_0 ./CS/lightCullingCS_ADVANCED.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T cs_6_0 ./CS/lightCullingCS_ADVANCED_DEBUG.hlsl -spirv -fspv-target-env=vulkan1.1
dxc -T cs_6_0 ./CS/lightCullingCS_DEBUG.hlsl -spirv -fspv-target-env=vulkan1.1