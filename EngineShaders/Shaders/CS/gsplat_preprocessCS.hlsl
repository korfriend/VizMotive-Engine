// HLSL Shader for Gaussian Screen-Space Covariance and Radius Computation
// Includes: Covariance Matrix Inversion (EWA Algorithm), Eigenvalues, and Bounding Rectangle Calculation

#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"
#include "../CommonHF/gaussianSplattingHF.hlsli"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   

PUSHCONSTANT(push, GaussianPushConstants);

#define SH_COEFF_STRIDE 16

RWByteAddressBuffer counterBuffer : register(u10); // fixed

RWTexture2D<unorm float4> inout_color : register(u0);
RWStructuredBuffer<uint> touchedTiles : register(u1);
RWStructuredBuffer<GaussianKernelAttribute> gaussianKernelAttributes : register(u2);

Buffer<float4> gaussianScale_Opacities : register(t0);
Buffer<float4> gaussianQuaterinions : register(t1);
Buffer<float> gaussianSHs : register(t2);

float3 get_sh_float3(Buffer<float> gaussianSHs, int index) {
    int start = index * 3;
    return float3(gaussianSHs[start + 0], gaussianSHs[start + 1], gaussianSHs[start + 2]);
}

float3 compute_sh(Buffer<float> gaussianSHs, float3 pos, int idx, float3 camPos)
{
    float3 dir = pos - camPos;
    float len = length(dir);
    dir = (len > 1e-6f) ? (dir / len) : float3(0.0f, 0.0f, 0.0f);

    int baseIndex = idx * SH_COEFF_STRIDE;

    float3 result = SH_C0 * get_sh_float3(gaussianSHs, baseIndex + 0);

    float x = dir.x;
    float y = dir.y;
    float z = dir.z;
    result = result
        - SH_C1 * y * get_sh_float3(gaussianSHs, baseIndex + 1)
        + SH_C1 * z * get_sh_float3(gaussianSHs, baseIndex + 2)
        - SH_C1 * x * get_sh_float3(gaussianSHs, baseIndex + 3);

    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float yz = y * z;
    float xz = x * z;

    result +=
        SH_C2[0] * xy * get_sh_float3(gaussianSHs, baseIndex + 4) +
        SH_C2[1] * yz * get_sh_float3(gaussianSHs, baseIndex + 5) +
        SH_C2[2] * (2.0f * zz - xx - yy) * get_sh_float3(gaussianSHs, baseIndex + 6) +
        SH_C2[3] * xz * get_sh_float3(gaussianSHs, baseIndex + 7) +
        SH_C2[4] * (xx - yy) * get_sh_float3(gaussianSHs, baseIndex + 8);

    result +=
        SH_C3[0] * y * (3.0f * xx - yy) * get_sh_float3(gaussianSHs, baseIndex + 9) +
        SH_C3[1] * xy * z * get_sh_float3(gaussianSHs, baseIndex + 10) +
        SH_C3[2] * y * (4.0f * zz - xx - yy) * get_sh_float3(gaussianSHs, baseIndex + 11) +
        SH_C3[3] * z * (2.0f * zz - 3.0f * xx - 3.0f * yy) * get_sh_float3(gaussianSHs, baseIndex + 12) +
        SH_C3[4] * x * (4.0f * zz - xx - yy) * get_sh_float3(gaussianSHs, baseIndex + 13) +
        SH_C3[5] * z * (xx - yy) * get_sh_float3(gaussianSHs, baseIndex + 14) +
        SH_C3[6] * x * (xx - 3.0f * yy) * get_sh_float3(gaussianSHs, baseIndex + 15);

    result += 0.5f;

    return max(result, 0.0f);
}
// Function: getrect, inline function in cuda(auxiliary)
void getRect(float2 p, int max_radius, uint2 grid, out uint2 rect_min, out uint2 rect_max)
{
    // Calculate rect_min
    rect_min.x = min(grid.x, max(0, (int) ((p.x - max_radius) / SH_COEFF_STRIDE)));
    rect_min.y = min(grid.y, max(0, (int) ((p.y - max_radius) / SH_COEFF_STRIDE)));

    // Calculate rect_max
    rect_max.x = min(grid.x, max(0, (int) ((p.x + max_radius + SH_COEFF_STRIDE - 1) / SH_COEFF_STRIDE)));
    rect_max.y = min(grid.y, max(0, (int) ((p.y + max_radius + SH_COEFF_STRIDE - 1) / SH_COEFF_STRIDE)));
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
float3 computeCov2D(float3 mean, float focal_length, uint2 resolution, float3x3 cov3D, float4x4 viewmatrix)
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
    uint idx = DTid.x;
    
    if (idx >= push.num_elements)
        return;

    // Load camera data
    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;
    float focalLength = camera.focal_length;
    float3 camPos = camera.position;

    // Load Position, Scale/Opacity, Quaternion, SH coefficients
    // bindless graphics, load buffer with index

    Buffer<float4> gsplatPosition = bindless_buffers_float4[push.vb_pos_w];
    float3 pos = gsplatPosition[idx].xyz;
    float4 scale_opacity = gaussianScale_Opacities[idx];
    float3 scale = scale_opacity.xyz;
    float opacity = scale_opacity.w;
    float4 rotation = gaussianQuaterinions[idx];

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
    uint2 rect_min, rect_max;   //uint2 grid = (78, 44);
    getRect(point_image, int(radius), uint2(W / GSPLAT_TILESIZE, H / GSPLAT_TILESIZE), rect_min, rect_max);

    uint total_tiles = (rect_max.x - rect_min.x) * (rect_max.y - rect_min.y);

    if (total_tiles == 0)
        return;

    //for (uint y = rect_min.y * GSPLAT_TILESIZE; y < rect_max.y * GSPLAT_TILESIZE; y++)
    //{
    //    for (uint x = rect_min.x * GSPLAT_TILESIZE; x < rect_max.x * GSPLAT_TILESIZE; x++)
    //    {
    //        inout_color[uint2(x, y)] = float4(1, 0, 0, 1);
    //    }
    //}
    //int2 pixel_coord = int2(point_image + 0.5f);
    //inout_color[pixel_coord] = float4(1, 1, 0, 1);

    counterBuffer.InterlockedAdd(GAUSSIANCOUNTER_OFFSET_TOUCHCOUNT, total_tiles);

    // compute RGB from SH coefficients
    float3 rgb_sh = compute_sh(gaussianSHs, pos, idx, camPos);
    float4 final_RGB = float4(rgb_sh, 1.0f);
    
    float4 p_view = mul(float4(pos, 1.0f), camera.view);
    touchedTiles[idx] = total_tiles;

    GaussianKernelAttribute at;

    at.conic_opacity = float4(conic.x, conic.y, conic.z, opacity);
    at.color_radii = float4(rgb_sh, radius);
    at.aabb = uint4(rect_min.x, rect_min.y, rect_max.x, rect_max.y);
    at.uv = point_image;
    at.depth = p_view.z;
    at.magic = 0x12345678;

    gaussianKernelAttributes[idx] = at;

    //if (pixel_coord.x >= 0 && pixel_coord.x < int(W) && pixel_coord.y >= 0 && pixel_coord.y < int(H))
    //{
    //    inout_color[pixel_coord] = float4(final_RGB);
    //}
    
    // for debugging bounding box
    //uint2 pixel_rect_min = rect_min * GSPLAT_TILESIZE;
    //uint2 pixel_rect_max = rect_max * GSPLAT_TILESIZE;
    
    //pixel_rect_min.x = min(pixel_rect_min.x, W);
    //pixel_rect_min.y = min(pixel_rect_min.y, H);
    //pixel_rect_max.x = min(pixel_rect_max.x, W);
    //pixel_rect_max.y = min(pixel_rect_max.y, H);
    
    //for (uint py = pixel_rect_min.y; py < pixel_rect_max.y; py++)
    //{
    //    for (uint px = pixel_rect_min.x; px < pixel_rect_max.x; px++)
    //    {
    //        if (px == pixel_rect_min.x || px == pixel_rect_max.x - 1 ||
    //            py == pixel_rect_min.y || py == pixel_rect_max.y - 1)
    //        {
    //            inout_color[uint2(px, py)] = float4(1.0f, 1.0f, 0.0f, 1.0f);
    //        }
    //    }
    //}
}