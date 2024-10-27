 dxc -T vs_6_0 ./VS/meshVS_debug.hlsl -spirv
 dxc -T vs_6_0 ./VS/meshVS_common.hlsl -spirv
 dxc -T vs_6_0 ./VS/meshVS_simple.hlsl -spirv
 dxc -T vs_6_0 ./VS/vertexcolorVS.hlsl -spirv

 dxc -T ps_6_0 ./PS/meshPS_debug.hlsl -spirv
 dxc -T ps_6_0 ./PS/meshPS_simple.hlsl -spirv
