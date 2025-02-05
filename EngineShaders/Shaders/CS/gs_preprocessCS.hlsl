// HLSL Shader for Gaussian Screen-Space Covariance and Radius Computation
// Includes: Covariance Matrix Inversion (EWA Algorithm), Eigenvalues, and Bounding Rectangle Calculation

#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   

static const float SH_C0 = 0.28209479177387814f;
static const float SH_C1 = 0.4886025119029199f;
static const float SH_C2[5] = {
    1.0925484305920792f,
    -1.0925484305920792f,
    0.31539156525252005f,
    -1.0925484305920792f,
    0.5462742152960396f
};
static const float SH_C3[7] = {
    -0.5900435899266435f,
    2.890611442640554f,
    -0.4570457994644658f,
    0.3731763325901154f,
    -0.4570457994644658f,
    1.445305721320277f,
    -0.5900435899266435f
};

PUSHCONSTANT(gaussians, GaussianPushConstants);

RWTexture2D<unorm float4> inout_color : register(u0);
RWStructuredBuffer<uint> touchedTiles : register(u1);
RWStructuredBuffer<uint> offsetTiles : register(u2);

// type XMFLOAT4
//StructuredBuffer<float4> gaussian_scale_opacity : register(t0);
//StructuredBuffer<float4> gaussian_rotation : register(t1);

float3 get_sh_float3(Buffer<float> gs_shs, int index) {
    int start = index * 3;
    return float3(gs_shs[start + 0], gs_shs[start + 1], gs_shs[start + 2]);
}

float3 compute_sh(Buffer<float> gs_shs, float3 pos, int idx, float3 camPos)
{
    float3 dir = pos - camPos;
    float len = length(dir);
    dir = (len > 1e-6f) ? (dir / len) : float3(0.0f, 0.0f, 0.0f);

    int baseIndex = idx * 16;

    float3 result = SH_C0 * get_sh_float3(gs_shs, baseIndex + 0);

    float x = dir.x;
    float y = dir.y;
    float z = dir.z;
    result = result
        - SH_C1 * y * get_sh_float3(gs_shs, baseIndex + 1)
        + SH_C1 * z * get_sh_float3(gs_shs, baseIndex + 2)
        - SH_C1 * x * get_sh_float3(gs_shs, baseIndex + 3);

    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float yz = y * z;
    float xz = x * z;

    result +=
        SH_C2[0] * xy * get_sh_float3(gs_shs, baseIndex + 4) +
        SH_C2[1] * yz * get_sh_float3(gs_shs, baseIndex + 5) +
        SH_C2[2] * (2.0f * zz - xx - yy) * get_sh_float3(gs_shs, baseIndex + 6) +
        SH_C2[3] * xz * get_sh_float3(gs_shs, baseIndex + 7) +
        SH_C2[4] * (xx - yy) * get_sh_float3(gs_shs, baseIndex + 8);

    result +=
        SH_C3[0] * y * (3.0f * xx - yy) * get_sh_float3(gs_shs, baseIndex + 9) +
        SH_C3[1] * xy * z * get_sh_float3(gs_shs, baseIndex + 10) +
        SH_C3[2] * y * (4.0f * zz - xx - yy) * get_sh_float3(gs_shs, baseIndex + 11) +
        SH_C3[3] * z * (2.0f * zz - 3.0f * xx - 3.0f * yy) * get_sh_float3(gs_shs, baseIndex + 12) +
        SH_C3[4] * x * (4.0f * zz - xx - yy) * get_sh_float3(gs_shs, baseIndex + 13) +
        SH_C3[5] * z * (xx - yy) * get_sh_float3(gs_shs, baseIndex + 14) +
        SH_C3[6] * x * (xx - 3.0f * yy) * get_sh_float3(gs_shs, baseIndex + 15);

    result += 0.5f;

    return max(result, 0.0f);
}
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
    float3 camPos = camera.position;
    // Load instance and geometry
    ShaderMeshInstance gs_instance = load_instance(gaussians.instanceIndex);
    uint subsetIndex = gaussians.geometryIndex - gs_instance.geometryOffset;
    ShaderGeometry geometry = load_geometry(subsetIndex);

    // Load Position, Scale/Opacity, Quaternion, SH coefficients
    // bindless graphics, load buffer with index
    Buffer<float4> gs_position = bindless_buffers_float4[geometry.vb_pos_w];
    Buffer<float4> gs_scale_opacity = bindless_buffers_float4[gaussians.gaussian_scale_opacities_index];
    Buffer<float4> gs_quaternion = bindless_buffers_float4[gaussians.gaussian_quaternions_index];
    //Buffer<float3> gs_shs = bindless_buffers_float3[gaussians.gaussian_SHs_index]; 
    Buffer<float> gs_shs = bindless_buffers_float[gaussians.gaussian_SHs_index]; // struct SH {XMFLOAT3 dcSHs[16];};

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
    uint2 rect_min, rect_max;   //uint2 grid = (78, 44);
    getRect(point_image, int(radius), uint2(W / 16, H / 16), rect_min, rect_max);

    uint total_tiles = (rect_max.x - rect_min.x) * (rect_max.y - rect_min.y);

    if (total_tiles == 0)
        return;

    InterlockedAdd(touchedTiles[idx], total_tiles);

    int2 pixel_coord = int2(point_image + 0.5f);

    // compute RGB from SH coefficients
    float3 rgb_sh = compute_sh(gs_shs, pos, idx, camPos);
    float4 final_RGB = float4(rgb_sh, 1.0f);

    if (pixel_coord.x >= 0 && pixel_coord.x < int(W) && pixel_coord.y >= 0 && pixel_coord.y < int(H))
    {
        inout_color[pixel_coord] = float4(final_RGB);
    }
}

// ========= check pos,scale,rot values =========
//// test value for idx == 0
//float3 t_pos = gs_position[0].xyz;
//float3 t_scale = gs_scale_opacity[0].xyz;
//float4 t_rot = gs_quaternion[0];

// t_pos = (0.580303, -3.68339, 3.44946)
// t_scale = (-4.44527, -4.37531, -5.52493)
// t_rot = (0.666832, 0.0965957, -0.328523, 0.0227409)
// focal length = 1.0f
//if (t_rot.x > -4.5f && t_rot.x < -4.4f) 
// {
//    inout_color[pixel_coord] = float4(1.0f, 1.0f, 0.0f, 1.0f);
// }

// ========= check radius =========
//if (pixel_coord.x >= 0 && pixel_coord.x < int(W) && pixel_coord.y >= 0 && pixel_coord.y < int(H))
//{
//    if (radius >= 4)
//    {
//        inout_color[pixel_coord] = float4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
//    }
//    else
//    {
//        inout_color[pixel_coord] = float4(0.0f, 1.0f, 1.0f, 1.0f); // Cyan
//    }
//}

// ========= check SH =========
//float aa = gs_shs[0]; // 2.20295
//float bb = gs_shs[1]; // 1.60472
//float cc = gs_shs[2]; // 1.68436
//
//float dd = gs_shs[3]; // 0.0468679
//float ee = gs_shs[4]; // 0.0708462
//float ff = gs_shs[5]; // 0.0480314

//uint test_idx = 16;
//float3 pos_test = gs_position[test_idx].xyz;
//float3 RGB_Test = compute_sh(gs_shs, pos_test, test_idx, camPos);
//float4 final_RGB = float4(RGB_Test, 1.0f);

// SH color for gaussian 16: [0.8455225  0.87772584 0.65956086]
// Position of vertex 16: [-0.23435153  1.1573098   2.2420747]

//    (2.20295, 1.60472, 1.68436)
//    (0.0468679, 0.0708462, 0.0480314)
//    (0.0516027, 0.0534793, 0.0578414)
//    (0.062336, 0.0456323, 0.00781638)
//    (-0.000345991, 0.0367759, 0.0178164)
//    (0.0103551, 0.014644, 0.0277609)
//    (0.024907, 0.0401756, 0.0676378)
//    (0.0726372, 0.0403042, 0.0436357)
//    (0.0782928, 0.0253747, -0.019868)
//    (0.0180557, 0.0233558, 0.0152131)
//    (0.0250393, -0.0112344, 0.0226496)
//    (0.0322682, 0.0525895, 0.0343003)
//    (0.0483671, 0.0382582, 0.0437218)
//    (0.0462263, 0.0305492, -0.0138234)
//    (-0.00977228, 0.024071, 0.0118135)
//    (-0.0111381, -0.0156093, 0.00291849)