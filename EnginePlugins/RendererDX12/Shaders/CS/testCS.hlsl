// HLSL Shader for Gaussian Screen-Space Covariance and Radius Computation
// Includes: Covariance Matrix Inversion (EWA Algorithm), Eigenvalues, and Bounding Rectangle Calculation

#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"

PUSHCONSTANT(gaussians, GaussianPushConstants);

RWTexture2D<unorm float4> inout_color : register(u0);
RWStructuredBuffer<uint> touchedTiles_0 : register(u1);

// type XMFLOAT4
//StructuredBuffer<float4> gaussian_scale_opacity : register(t0);
//StructuredBuffer<float4> gaussian_rotation : register(t1);

// Function: getrect, inline function in cuda(auxiliary)
void getRect(float2 p, int max_radius, uint2 grid, out uint2 rect_min, out uint2 rect_max)
{
    const uint BLOCK_X = 16;
    const uint BLOCK_Y = 16;

    // Calculate rect_min
    rect_min.x = min(grid.x, max(0, (int) ((p.x - max_radius) / BLOCK_X)));
    rect_min.y = min(grid.y, max(0, (int) ((p.y - max_radius) / BLOCK_Y)));

    // Calculate rect_max
    rect_max.x = min(grid.x, max(0, (int) ((p.x + max_radius + BLOCK_X - 1) / BLOCK_X)));
    rect_max.y = min(grid.y, max(0, (int) ((p.y + max_radius + BLOCK_Y - 1) / BLOCK_Y)));
}

// Function: Convert world position to pixel coordinates
float2 worldToPixel(float3 pos, ShaderCamera camera, uint W, uint H)
{
    float4 p_view = mul(float4(pos, 1.0f), camera.view);
    float4 p_hom = mul(p_view, camera.projection);
    float p_w = 1.0f / max(p_hom.w, 1e-7f);
    float3 p_proj = float3(p_hom.x * p_w, p_hom.y * p_w, p_hom.z * p_w);

    // Convert NDC (-1~1) to screen coordinates (0~W, 0~H)
    return float2(
        (p_proj.x * 0.5f + 0.5f) * (float) W,
        (p_proj.y * 0.5f + 0.5f) * (float) H
    );
}

// Function: Compute the 3D covariance matrix for a Gaussian
float3x3 computeCov3D(float3 scale, float4 rotation)
{
    // Create scaling matrix
    float3x3 S = float3x3(
        scale.x, 0.0f, 0.0f,
        0.0f, scale.y, 0.0f,
        0.0f, 0.0f, scale.z
    );

    // Normalize quaternion
    float r = rotation.x;
    float x = rotation.y;
    float y = rotation.z;
    float z = rotation.w;

    // Compute rotation matrix from quaternion
    float3x3 R = float3x3(
        1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - r * z), 2.0f * (x * z + r * y),
        2.0f * (x * y + r * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - r * x),
        2.0f * (x * z - r * y), 2.0f * (y * z + r * x), 1.0f - 2.0f * (x * x + y * y)
    );

    // Compute the transformation matrix
    float3x3 M = mul(S, R);

    // Compute 3D covariance matrix
    float3x3 Sigma = mul(transpose(M), M);

    // Return the covariance matrix (upper triangular matrix)
    return float3x3(
        Sigma[0][0], Sigma[0][1], Sigma[0][2],
        0.0f, Sigma[1][1], Sigma[1][2],
        0.0f, 0.0f, Sigma[2][2]
    );
}

// Function: Compute the 2D covariance matrix for a Gaussian
float3 computeCov2D(
    float3 mean,
    float focal_length, // Single focal length
    uint2 resolution, // Screen resolution
    float3x3 cov3D, // 3D covariance matrix
    float4x4 viewmatrix // Camera view matrix
)
{
    // Calculate aspect ratio
    float aspect_ratio = (float) resolution.x / (float) resolution.y;

    // Compute focal_x and focal_y from single focal_length
    float focal_x = focal_length * aspect_ratio;
    float focal_y = focal_length;

    // Transform the point to view space
    float4 t = mul(viewmatrix, float4(mean, 1.0f));
    float3 t_view = t.xyz / t.w;

    // Compute Jacobian matrix
    float3x3 J = float3x3(
        focal_x / t_view.z, 0.0f, -(focal_x * t_view.x) / (t_view.z * t_view.z),
        0.0f, focal_y / t_view.z, -(focal_y * t_view.y) / (t_view.z * t_view.z),
        0.0f, 0.0f, 0.0f
    );

    // Compute transformation matrix
    float3x3 W = float3x3(
        viewmatrix[0].xyz,
        viewmatrix[1].xyz,
        viewmatrix[2].xyz
    );

    float3x3 T = mul(W, J);

    // Compute 2D covariance matrix
    float3x3 cov = mul(transpose(T), mul(cov3D, T));

    // Apply low-pass filter: every Gaussian should be at least
    // one pixel wide/high. Discard 3rd row and column.
    cov[0][0] += 0.3f;
    cov[1][1] += 0.3f;

    // Return the 2D covariance matrix (diagonal matrix)
    return float3(cov[0][0], cov[0][1], cov[1][1]);
}

[numthreads(256, 1, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    float radius = 0.0f;
    // Global thread index along X-axis
    uint idx = DTid.x;

    // Exit if out of range
    if (idx >= gaussians.num_gaussians)
        return;

    // Load camera data
    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;
    float focalLength = camera.focal_length;

    // Load instance and geometry
    ShaderMeshInstance gs_instance = load_instance(gaussians.instanceIndex);
    uint subsetIndex = gaussians.geometryIndex - gs_instance.geometryOffset;
    ShaderGeometry geometry = load_geometry(subsetIndex);

    // Load Position, Scale/Opacity, Quaternion
    // bindless graphics, load buffer with index
    Buffer<float4> gs_position = bindless_buffers_float4[geometry.vb_pos_w];
    Buffer<float4> gs_scale_opacity = bindless_buffers_float4[gaussians.gaussian_scale_opacities_index];
    Buffer<float4> gs_quaternion = bindless_buffers_float4[gaussians.gaussian_quaternions_index];

    float3 pos = gs_position[idx].xyz;
    float3 scale = gs_scale_opacity[idx].xyz;
    float opacity = gs_scale_opacity[idx].w;
    float4 rotation = gs_quaternion[idx];

    // computeCov3D
    float3x3 cov3D = computeCov3D(scale, rotation);

    // computeCov2D
    float3 cov2D = computeCov2D(pos, focalLength, uint2(W, H), cov3D, camera.view);

    // Invert 2D covariance matrix (EWA algorithm)
    float det = (cov2D.x * cov2D.z - cov2D.y * cov2D.y);
    if (det == 0.0f)
        return;

    float det_inv = 1.0f / det;
    float3 conic = float3(cov2D.z * det_inv, -cov2D.y * det_inv, cov2D.x * det_inv);

    // compute eigenvalue l1, l2 and radius
    float mid = 0.5f * (cov2D.x + cov2D.z);
    float lambda1 = mid + sqrt(max(0.1f, mid * mid - det));
    float lambda2 = mid - sqrt(max(0.1f, mid * mid - det));
    radius = ceil(3.0f * sqrt(max(lambda1, lambda2)));
    float2 point_image = worldToPixel(pos, camera, W, H);

    // bounding box
    uint2 rect_min, rect_max;
    //uint2 grid = (78, 44);
    //getRect(point_image, int(radius), uint2(W / 16, H / 16), rect_min, rect_max);
    // ============== get rect ==============


    uint total_tiles = (rect_max.x - rect_min.x) * (rect_max.y - rect_min.y);

    if (total_tiles == 0)
        return;

    // --- ����� �κ�: bounding box �� Ÿ�ϸ��� InterlockedAdd( touchedTiles_0[idx], 1 ) ---
    for (uint ty = rect_min.y; ty < rect_max.y; ty++)
    {
        for (uint tx = rect_min.x; tx < rect_max.x; tx++)
        {
            InterlockedAdd(touchedTiles_0[idx], 1);
        }
    }
    // -----------------------------------------------------------


    int2 pixel_coord = int2(point_image + 0.5f);

    // test value for idx == 0
    float3 t_pos = gs_position[0].xyz;
    float3 t_scale = gs_scale_opacity[0].xyz;
    float4 t_rot = gs_quaternion[0];

    // t_pos = (0.580303, -3.68339, 3.44946)
    // t_scale = (-4.44527, -4.37531, -5.52493)
    // t_rot = (0.666832, 0.0965957, -0.328523, 0.0227409)
    // focal length = 1.0f


    //if (t_rot.x > -4.5f && t_rot.x < -4.4f) {
    //    inout_color[pixel_coord] = float4(1.0f, 1.0f, 0.0f, 1.0f);
    //}

    if (pixel_coord.x >= 0 && pixel_coord.x < int(W) && pixel_coord.y >= 0 && pixel_coord.y < int(H))
    {
        if (touchedTiles_0[3323] >= 3)
        {
            inout_color[pixel_coord] = float4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
        }
        else
        {
            inout_color[pixel_coord] = float4(0.0f, 1.0f, 1.0f, 1.0f); // Cyan
        }
    }
}