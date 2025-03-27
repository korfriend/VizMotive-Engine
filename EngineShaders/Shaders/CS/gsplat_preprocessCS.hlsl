// HLSL Shader for Gaussian Screen-Space Covariance and Radius Computation
// Includes: Covariance Matrix Inversion (EWA Algorithm), Eigenvalues, and Bounding Rectangle Calculation

#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"

PUSHCONSTANT(push, GaussianPushConstants);

#define SH_COEFF_STRIDE 16

RWStructuredBuffer<uint> touchedTiles : register(u0);
RWStructuredBuffer<GaussianKernelAttribute> gaussianKernelAttributes : register(u1);
RWStructuredBuffer<uint> offsetTiles : register(u2);
RWByteAddressBuffer counterBuffer : register(u3);

RWTexture2D<unorm float4> inout_color : register(u10);

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

void getRect(float2 p, float max_radius, uint2 grid, out uint2 rect_min, out uint2 rect_max)
{
    // Calculate rect_min
    rect_min.x = min(grid.x, max(0, (int)((p.x - max_radius) / GSPLAT_TILESIZE)));
    rect_min.y = min(grid.y, max(0, (int)((p.y - max_radius) / GSPLAT_TILESIZE)));

    // Calculate rect_max
    rect_max.x = min(grid.x, max(0, (int)((p.x + max_radius + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE)));
    rect_max.y = min(grid.y, max(0, (int)((p.y + max_radius + GSPLAT_TILESIZE - 1) / GSPLAT_TILESIZE)));
}
// Function: Convert world position to pixel coordinates
float2 projToPixel(float3 p_proj, uint W, uint H)
{
    // Convert NDC (-1~1) to screen coordinates (0~W, 0~H)
    return float2(
        (p_proj.x + 1.0f) * 0.5f * (float)W,
        (1.0f - p_proj.y) * 0.5f * (float)H
    );
}

// Determines if a point (given by index) is inside the view frustum.
// Parameters:
//   idx         - Index of the point in the orig_points array (assumes 3 floats per point)
//   orig_points - Array of floats containing the original points (x,y,z sequentially)
//   viewmatrix  - The view transformation matrix (float4x4)
//   projmatrix  - The projection matrix (float4x4)
//   p_view      - Output: point transformed into view space
bool in_frustum(
    in float3 p_ws, // Assumes orig_points is bound as a constant buffer or StructuredBuffer
    float4x4 viewmatrix,
    float4x4 projmatrix,
    out float3 p_view,
    out float3 p_proj)
{
    // Transform the original point into view space using the view matrix.
    float4 p_view_h = mul(viewmatrix, float4(p_ws, 1));
    p_view = p_view_h.xyz / p_view_h.w;

    // If the point is too close (or behind) the near plane (here, z <= 0.2), it's considered outside the frustum.
    if (p_view.z >= -0.2f) // note: viewing direction
    {
        // In HLSL we cannot print or trap execution, so we simply return false.
        // In a debugging scenario you might set a flag or output to a debug buffer.
        return false;
    }

    // Bring the point into clip space using the projection matrix.
    float4 p_proj_h = mul(projmatrix, float4(p_view, 1));
    p_proj = p_proj_h.xyz / p_proj_h.w;

    if (p_proj.x < -1.3 || p_proj.x > 1.3 || p_proj.y < -1.3 || p_proj.y > 1.3)
    {
        return false;
    }

    // Otherwise, the point is inside the frustum.
    return true;
}

// Forward method for converting scale and rotation properties of each
// Gaussian to a 3D covariance matrix in world space. Also takes care
// of quaternion normalization.
// Computes the 3D covariance matrix (symmetric) from scale, modulation, and rotation.
// The resulting symmetric matrix is stored as 6 floats in the 'cov3D' array:
// [ cov3D[0]=Sigma(0,0), cov3D[1]=Sigma(0,1), cov3D[2]=Sigma(0,2),
//   cov3D[3]=Sigma(1,1), cov3D[4]=Sigma(1,2), cov3D[5]=Sigma(2,2) ]
void computeCov3D(float3 scale, float mod, float4 rot, out float cov3D[6])
{
    // Step 1: Create the scaling matrix S.
    // S is a diagonal matrix with modulated scale values.
    float3x3 S = float3x3(
        mod * scale.x, 0.0f, 0.0f,
        0.0f, mod * scale.y, 0.0f,
        0.0f, 0.0f, mod * scale.z
    );

    // Step 2: Normalize quaternion if necessary.
    // The original code commented out the normalization, assuming the quaternion is already normalized.
    // For now, we assume 'rot' is normalized.
    float r = rot.x;  // Scalar part
    float x = rot.y;
    float y = rot.z;
    float z = rot.w;

    // Step 3: Compute the rotation matrix R from the quaternion.
    // The rotation matrix is constructed using standard quaternion-to-matrix formulas.
    float3x3 R = float3x3(
        1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - r * z), 2.0f * (x * z + r * y),
        2.0f * (x * y + r * z), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - r * x),
        2.0f * (x * z - r * y), 2.0f * (y * z + r * x), 1.0f - 2.0f * (x * x + y * y)
    );

    // Step 4: Multiply the scaling and rotation matrices: M = S * R.
    // Note: In HLSL, the mul() function is used for matrix multiplication.
    float3x3 M = mul(S, R);

    // Step 5: Compute the 3D covariance matrix (Sigma) as Sigma = transpose(M) * M.
    float3x3 Sigma = mul(transpose(M), M);

    // Step 6: Store only the upper-triangle elements (due to symmetry) in the cov3D array.
    cov3D[0] = Sigma[0][0];
    cov3D[1] = Sigma[0][1];
    cov3D[2] = Sigma[0][2];
    cov3D[3] = Sigma[1][1];
    cov3D[4] = Sigma[1][2];
    cov3D[5] = Sigma[2][2];
}

// Function: Compute the 3D covariance matrix for a Gaussian
float3x3 computeCov3D_OLD(float3 scale, float4 rotation)
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

// Function: Compute the 2D covariance matrix for a Gaussian:
//      The following models the steps outlined by equations 29
//      and 31 in "EWA Splatting" (Zwicker et al., 2002). 
//      Additionally considers aspect / scaling of viewport.
//      Transposes used to account for row-/column-major conventions.
float3 computeCov2D(
    float3 mean,         // Mean point in 3D space
    float focal_x,       // Focal length in x (in pixel units)
    float focal_y,       // Focal length in y (in pixel units)
    float tan_fovx,      // Tangent of half the horizontal field of view
    float tan_fovy,      // Tangent of half the vertical field of view
    float cov3D[6],      // 3D covariance matrix (provided as a float3x3)
    float4x4 viewmatrix  // View transformation matrix (4x4)
)
{
    // Step 1: Transform the mean point using the view matrix.
    float4 t_h = mul(viewmatrix, float4(mean, 1));
    float3 t = t_h.xyz / t_h.w;

    // Step 2: Clamp the x and y components based on FOV limits (EWA Splatting, eq.29 & eq.31)
    float limx = 1.3f * tan_fovx;
    float limy = 1.3f * tan_fovy;
    float txtz = t.x / t.z;
    float tytz = t.y / t.z;
    t.x = min(limx, max(-limx, txtz)) * t.z;
    t.y = min(limy, max(-limy, tytz)) * t.z;

    // Step 3: Construct the Jacobian matrix J.
    // J is defined as:
    // [ focal_x/t.z,              0,  -focal_x*t.x/(t.z*t.z) ]
    // [           0,       focal_y/t.z, -focal_y*t.y/(t.z*t.z) ]
    // [           0,              0,                    0   ]
    //float3x3 J = float3x3(
    //    float3(focal_x / t.z, 0.0f, 0.0f),
    //    float3(0.0f, focal_y / t.z, 0.0f),
    //    float3(-(focal_x * t.x) / (t.z * t.z), -(focal_y * t.y) / (t.z * t.z), 0.0f)
    //);
    //float3x3 J = float3x3(
    //    float3(focal_x / t.z, 0.0f, -(focal_x * t.x) / (t.z * t.z)),
    //    float3(0.0f, focal_y / t.z, -(focal_y * t.y) / (t.z * t.z)),
    //    float3(0.0f, 0.0f, 0.0f)
    //);
    float3x3 J = float3x3(
        focal_x / t.z, 0.0f, -(focal_x * t.x) / (t.z * t.z),
        0.0f, focal_y / t.z, -(focal_y * t.y) / (t.z * t.z),
        0.0f, 0.0f, 0.0f
    );

    // Step 4: Extract the upper-left 3x3 part of the view matrix.
    // HLSL matrices are column-major by default, so each element (viewmatrix[i].xyz) is a column.
    //float3x3 W = float3x3(
    //    viewmatrix[0].xyz, // first column: (m00, m10, m20)
    //    viewmatrix[1].xyz, // second column: (m01, m11, m21)
    //    viewmatrix[2].xyz  // third column: (m02, m12, m22)
    //);
    float3x3 W = float3x3(
        viewmatrix[0].x, viewmatrix[0].y, viewmatrix[0].z,
        viewmatrix[1].x, viewmatrix[1].y, viewmatrix[1].z,
        viewmatrix[2].x, viewmatrix[2].y, viewmatrix[2].z
    );

    // Step 5: Compute the composite matrix T = W * J.
    float3x3 T = mul(W, J);

    // Step 6: Use the provided 3D covariance matrix.
    // In the GLM version, cov3D is built from an array, but here we assume it's already a float3x3.
    //float3x3 Vrk = cov3D;
    float3x3 Vrk = float3x3(
        float3(cov3D[0], cov3D[1], cov3D[2]),
        float3(cov3D[1], cov3D[3], cov3D[4]),
        float3(cov3D[2], cov3D[4], cov3D[5])
    );

    // Step 7: Compute the 2D covariance matrix:
    // cov = transpose(T) * transpose(Vrk) * T.
    // Since Vrk is symmetric, transpose(Vrk) equals Vrk, but we follow the original formulation.
    float3x3 cov = mul(mul(transpose(T), transpose(Vrk)), T);

    // Step 8: Return the 2D covariance elements as a float3:
    // [ cov(0,0), cov(0,1), cov(1,1) ]
    return float3(cov[0][0], cov[0][1], cov[1][1]);
}

float fov2focal(const float fov, const float pixels)
{
    return pixels / (2 * tan(fov / 2));
}

float focal2fov(const float focal, const float pixels)
{
    return 2.f * atan(pixels / (2 * focal));
}

// Function: Compute the 2D covariance matrix for a Gaussian
float3 computeCov2D_OLD(float3 mean, float focal_length, uint2 resolution, float3x3 cov3D, float4x4 viewmatrix)
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

[numthreads(GSPLAT_GROUP_SIZE, 1, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
    uint idx = DTid.x;
    
    if (idx >= push.numGaussians)
        return;

    // Load camera data
    ShaderCamera camera = GetCamera();
    uint W = camera.internal_resolution.x;
    uint H = camera.internal_resolution.y;
    float3 camPos = camera.position;

    // Load Position, Scale/Opacity, Quaternion, SH coefficients
    // bindless graphics, load buffer with index

    Buffer<float4> gsplatPosition = bindless_buffers_float4[push.geometryIndex];
    float3 pos = gsplatPosition[idx].xyz;

    ShaderMeshInstance inst = load_instance(push.instanceIndex);
    float4 pos_ws_h = mul(inst.transform.GetMatrix(), float4(pos, 1));
    float3 pos_ws = pos_ws_h.xyz / pos_ws_h.w;
    pos_ws = pos;   // TEST //

    float3 p_view, p_proj;
    if (!in_frustum(pos_ws, camera.view, camera.projection, p_view, p_proj))
        return;

    float4 scale_opacity = gaussianScale_Opacities[idx];
    float3 scale = scale_opacity.xyz;
    float opacity = scale_opacity.w;
    float4 rotation = gaussianQuaterinions[idx];

    // TODO: 
    // once Gaussian Splatting computation is completed, no need to compute cov3D
    // , which mean cov3D can be stored as a fixed parameter
    float cov3D[6];
    computeCov3D(scale, 1.f, rotation, cov3D);

    float fovX = focal2fov(push.focalX, W);
    float fovY = focal2fov(push.focalY, H);
    float tanfovX = tan(fovX * 0.5);
    float tanfovY = tan(fovY * 0.5);

    // Compute 2D screen-space covariance matrix
    float3 cov2D = computeCov2D(pos_ws, push.focalX, push.focalY, tanfovX, tanfovY, cov3D, camera.view);

    const float det_cov = cov2D.x * cov2D.z - cov2D.y * cov2D.y;
    const float h_var = 0.3f;
    //cov2D.x += h_var;
    //cov2D.z += h_var;
    const float det_cov_plus_h_cov = cov2D.x * cov2D.z - cov2D.y * cov2D.y;
    float h_convolution_scaling = 1.0f;

    if (push.flags & GSPLAT_FLAG_ANTIALIASING)
        h_convolution_scaling = sqrt(max(0.000025f, det_cov / det_cov_plus_h_cov)); // max for numerical stability

    // Invert covariance (EWA algorithm)
    const float det = det_cov_plus_h_cov;

    if (det == 0.0f) // apply eps
        return;

    float det_inv = 1.0f / det;
    float3 conic = float3(cov2D.z * det_inv, -cov2D.y * det_inv, cov2D.x * det_inv);

    // Compute extent in screen space (by finding eigenvalues of
    // 2D covariance matrix). Use extent to compute a bounding rectangle
    // of screen-space tiles that this Gaussian overlaps with. Quit if
    // rectangle covers 0 tiles. 
    float mid = 0.5f * (cov2D.x + cov2D.z);
    float lambda1 = mid + sqrt(max(0.1f, mid * mid - det));
    float lambda2 = mid - sqrt(max(0.1f, mid * mid - det));
    float radius = ceil(3.0f * sqrt(max(lambda1, lambda2)));
    float2 point_image = projToPixel(p_proj, W, H);

    // bounding box
    uint2 rect_min, rect_max;
    getRect(point_image, radius, uint2(push.tileWidth, push.tileHeight), rect_min, rect_max);

    uint total_tiles = (rect_max.x - rect_min.x) * (rect_max.y - rect_min.y);
    if (total_tiles == 0)
        return;

    uint offset;
    counterBuffer.InterlockedAdd(GAUSSIANCOUNTER_OFFSET_TOUCHCOUNT, total_tiles, offset);
    offsetTiles[idx] = offset;

    // compute RGB from SH coefficients
    // note: here, use original pos
    float3 rgb_sh = compute_sh(gaussianSHs, pos, idx, camPos);
    float4 final_RGB = float4(rgb_sh, 1.0f);
    
    touchedTiles[idx] = total_tiles;

    GaussianKernelAttribute gussial_attribute;

    gussial_attribute.conic_opacity = float4(conic.x, conic.y, conic.z, opacity * h_convolution_scaling);
    gussial_attribute.color_radii = float4(rgb_sh, radius);
    gussial_attribute.aabb = uint4(rect_min.x, rect_min.y, rect_max.x, rect_max.y);
    gussial_attribute.uv = point_image;
    gussial_attribute.depth = -p_view.z;
    gussial_attribute.magic = 0x12345678;

    gaussianKernelAttributes[idx] = gussial_attribute;


    // TEST
    //for (uint y = rect_min.y * GSPLAT_TILESIZE; y < rect_max.y * GSPLAT_TILESIZE; y++)
    //{
    //    for (uint x = rect_min.x * GSPLAT_TILESIZE; x < rect_max.x * GSPLAT_TILESIZE; x++)
    //    {
    //        inout_color[uint2(x, y)] = float4(rgb_sh, 1);
    //    }
    //}
    int2 pixel_coord = int2(point_image + 0.5f);
    inout_color[pixel_coord] = float4(rgb_sh, 1);
}