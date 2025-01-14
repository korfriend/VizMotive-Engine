// HLSL Shader for Gaussian Screen-Space Covariance and Radius Computation
// Includes: Covariance Matrix Inversion (EWA Algorithm), Eigenvalues, and Bounding Rectangle Calculation

#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"

PUSHCONSTANT(gaussians, GaussianPushConstants);

RWTexture2D<unorm float4> inout_color : register(u0);
RWStructuredBuffer<uint> touchedTiles_0 : register(u1);

// Function: Get screen-space bounding rectangle for Gaussian
void getRect(float2 p, int max_radius, uint2 grid, out uint2 rect_min, out uint2 rect_max)
{
    // Define block dimensions (BLOCK_X and BLOCK_Y need to match grid setup)
    const int BLOCK_X = 16; // Tile width
    const int BLOCK_Y = 16; // Tile height

    // Calculate rect_min
    rect_min.x = min(grid.x, max(0, (int)((p.x - max_radius) / BLOCK_X)));
    rect_min.y = min(grid.y, max(0, (int)((p.y - max_radius) / BLOCK_Y)));

    // Calculate rect_max
    rect_max.x = min(grid.x, max(0, (int)((p.x + max_radius + BLOCK_X - 1) / BLOCK_X)));
    rect_max.y = min(grid.y, max(0, (int)((p.y + max_radius + BLOCK_Y - 1) / BLOCK_Y)));
}

// Function: Convert world position to pixel coordinates
float2 worldToPixel(float3 pos, ShaderCamera camera, uint W, uint H)
{
    float4 p_view = mul(float4(pos, 1.0f), camera.view);
    float4 p_hom = mul(p_view, camera.projection);

    // Avoid division by zero
    float p_w = 1.0f / max(p_hom.w, 1e-7f);

    // Convert to Normalized Device Coordinates (NDC)
    float3 p_proj = float3(p_hom.x * p_w, p_hom.y * p_w, p_hom.z * p_w);

    // Convert NDC (-1~1) to screen coordinates (0~W, 0~H)
    return float2(
        (p_proj.x * 0.5f + 0.5f) * (float)W,
        (p_proj.y * 0.5f + 0.5f) * (float)H
    );

    //// Round to nearest pixel and return
    //return int2(screen_coords + 0.5f);
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
        0.0f,        Sigma[1][1], Sigma[1][2],
        0.0f,        0.0f,        Sigma[2][2]
    );
}

// Function: Compute the 2D covariance matrix for a Gaussian
float3 computeCov2D(
    float3 mean,
    float focal_length, // Single focal length
    uint2 resolution,   // Screen resolution
    float3x3 cov3D,     // 3D covariance matrix
    float4x4 viewmatrix // Camera view matrix
)
{
    // Calculate aspect ratio
    float aspect_ratio = (float)resolution.x / (float)resolution.y;

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

    // Return the 2D covariance matrix (compressed form)
    return float3(cov[0][0], cov[0][1], cov[1][1]);
}

// Main Compute Shader
[numthreads(256, 1, 1)]
void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
{
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

    Buffer<float4> vb_pos_w = bindless_buffers_float4[geometry.vb_pos_w];
    float3 pos = vb_pos_w[idx].xyz;

    // computeCov3D
    float3 scale = float3(1.0, 1.0, 1.0);         // Replace with actual scale
    float4 rotation = float4(1.0, 0.0, 0.0, 0.0); // Replace with actual rotation
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
    float radius = ceil(3.0f * sqrt(max(lambda1, lambda2)));
    float2 point_image = worldToPixel(pos, camera, W, H);

    // bounding box
    uint2 rect_min, rect_max;
    getRect(point_image, int(radius), uint2(W / 16, H / 16), rect_min, rect_max);

    if ((rect_max.x - rect_min.x) * (rect_max.y - rect_min.y) == 0)
        return;

    int2 pixel_coord = int2(point_image + 0.5f);

    if (pixel_coord.x >= 0 && pixel_coord.x < int(W) && pixel_coord.y >= 0 && pixel_coord.y < int(H))
    {
        inout_color[pixel_coord] = float4(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
    }

    // Store results or further process here...
    touchedTiles_0[idx] = (rect_max.y - rect_min.y) * (rect_max.x - rect_min.x);
}
