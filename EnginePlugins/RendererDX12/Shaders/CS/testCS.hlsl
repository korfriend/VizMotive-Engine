#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"

PUSHCONSTANT(gaussians, GaussianPushConstants);

//RWTexture2D<float> inout_linear_depth : register(u1);
RWTexture2D<unorm float4> inout_color : register(u0);

[numthreads(256, 1, 1)]
// todo : sync with dispatch
// to deal with thread id individually, using Gid instread of DTid

// p_hom = transformpoint4x4(pos, projmatrix)
// p_w = 1 / (p_hom.w + 0.00000001f);
// p_proj = {p_hom.x * p_w, p_hom.y * p_w, p_hom.z * p_w};

void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    //const uint2 pixel = DTid.xy;
    
    // Global thread index in the dispatch space along the X-axis
    uint idx = DTid.x; // auto idx = cg::this_grid().thread_rank();

    // for bounding box
    uint2 rect_min, rect_max;

    // camera
    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;

    //const bool pixel_valid = (pixel.x < W / 2) && (pixel.y < H / 2);
    //if (!pixel_valid)
    //{
    //    return;
    //}
    // inout_color[pixel] = float4(1, 1, 0, 1);

    // ply... position, normal...
    // note : ply file store all the normal values as a zero

    ShaderMeshInstance gs_instance = load_instance(gaussians.instanceIndex);
    uint subsetIndex = gaussians.geometryIndex - gs_instance.geometryOffset;
    ShaderGeometry geometry = load_geometry(subsetIndex);

    if (idx >= gaussians.num_gaussians) return;
    
    // tile touch, UINT (4 bytes)
    // RWByteAddressBuffer tiletouch = bindless_rwbuffers[gaussians.touchedTiles_0_index];

    // position
    Buffer<float4> vb_pos_w = bindless_buffers_float4[geometry.vb_pos_w];
    // tile touch count buffer
    //Buffer<uint> tile_touched = bindless_buffers_uint[gaussians.touchedTiles_0_index];  
    // read only
    //RWStructuredBuffer<uint> tile_touched = bindless_buffers_uint[guassians.touchedTiles_0_index];
    // atomic add, interlockedAdd

    {
        float3 pos = vb_pos_w[idx].xyz;

        // view, proj matrix
        float4x4 viewmatrix = camera.view;
        float4x4 projmatrix = camera.projection;

        // Transform position using projection matrix
        float4 p_view = mul(float4(pos, 1.0f), viewmatrix);  // world to view
        float4 p_hom = mul(p_view, projmatrix);             // view to clip
        float p_w = 1.0f / (p_hom.w + 0.0000001f);
        float3 p_proj = float3(p_hom.x * p_w, p_hom.y * p_w, p_hom.z * p_w);

        // ndc 2 pix
        float2 point_image = float2(
            ((p_proj.x + 1.0) * W - 1.0) * 0.5,
            ((p_proj.y + 1.0) * H - 1.0) * 0.5
        );

        // float to uint for targeting exact pixel
        uint2 point_pixel = uint2(
            clamp((uint)point_image.x, 0u, W - 1),
            clamp((uint)point_image.y, 0u, H - 1)
        );

        inout_color[point_pixel] = float4(1, 1, 0, 1); // Yellow
    }

    //uint idx = DTid.y * 16 + DTid.x; // 1D array index, not 2D
    // 
    //float2 normalized_image = float2(
    //    point_image.x / W,    
    //    point_image.y / H
    //);

    //// Use normalized_image as color
    //inout_color[pixel] = float4(normalized_image, pos.z, 1);

    //for (int i = 0; i < 16; ++i)
    //{
    //    for (int j = 0; j < 16; ++j)
    //    {
    //        uint2 offset_pixel = pixel + uint2(i, j); // add offset
    //        inout_color[offset_pixel] = float4(pos.x, pos.y, pos.z, 1);
    //    }
    //}
    // 
    //uint size = gaussians.num_gaussians;
    //if (size > 10)
    //{
    //    inout_color[pixel] = float4(1, 1, 0, 1);
    //}    

    // test vals
    // vertex 0
    //inout_color[pixel] = float4(pos.x - 0.580303, pos.y + 3.68339, pos.z - 3.44946, 1); 
    // vertex 1
    //inout_color[pixel] = float4(pos.x - 2.08206, pos.y + 4.22113, pos.z - 3.22497, 1);
    // vertex 2
    //inout_color[pixel] = float4(pos.x - 0.0880209 + 1, pos.y - 2.32696 + 1, pos.z - 3.20269 + 1, 1);

    //inout_color[pixel] = float4(pos.x, pos.y, pos.z, 1);
}
