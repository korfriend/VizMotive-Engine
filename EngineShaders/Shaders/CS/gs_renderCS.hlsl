    #include "../Globals.hlsli"
    #include "../ShaderInterop_GS.h"
    #include "../CommonHF/surfaceHF.hlsli"
    #include "../CommonHF/raytracingHF.hlsli"   
    // test210
    //PUSHCONSTANT(gaussians, GaussianPushConstants);
    PUSHCONSTANT(sortConst, GaussianSortConstants);

    //Buffer<float4> gs_vertexAttr = bindless_buffers_float4[gaussiansSorts.gaussian_Vertex_Attributes_index];
    // vertexAttrs consists of 16 elements (4 + 4 + 4 + 4 = 16)
    // first 4 elements : (conic params + opacity)
    // second 4 elements : color + radius
    // third 4 elements : bounding box (aabb)
    // fourth 4 elements : uv + depth + padding

    RWTexture2D<unorm float4> inout_color : register(u0);
    RWStructuredBuffer<uint> totalSumBufferHost : register(u1);

    StructuredBuffer<VertexAttribute> Vertices : register(t0);
    StructuredBuffer<uint> tileBoundaryBuffer : register(t1);       // tileBoundaryBuffer
    StructuredBuffer<uint64_t> sortKBufferEven : register(t2);         // sortKBufferEven
    StructuredBuffer<uint> sortVBufferEven : register(t3);          // sortVBufferEven

    [numthreads(256, 1, 1)] // 16 x 16 x 1
    void main(uint2 Gid : SV_GroupID, uint2 DTid : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex)
    {
        ShaderCamera camera = GetCamera();
        uint W = camera.internal_resolution.x;
        uint H = camera.internal_resolution.y;

        uint idx = DTid.x;
        Buffer<uint> totalPrefixSum = bindless_buffers_uint[sortConst.totalSumBufferHost_index];
        uint totalSum = totalPrefixSum[0];

        VertexAttribute v = Vertices[idx];

        int2 pixel_coord = int2(v.uv);

        if (pixel_coord.x >= 0 && pixel_coord.x < int(W) && pixel_coord.y >= 0 && pixel_coord.y < int(H))
        {
            //if (v.color_radii.w >= 4)
            //if ( (v.aabb.x >= 8 && v.aabb.y >= 8) && (v.aabb.z <= 12 && v.aabb.w <= 12))
            //if(tileBoundaryBuffer[98765] == 0)
            uint64_t fullKey = sortKBufferEven[0];
            uint tileIndex = (uint)(fullKey >> 32);
            uint depth = (uint)(fullKey & 0xFFFFFFFF);

            if(tileBoundaryBuffer[3300] >= 100000)
            {
                inout_color[pixel_coord] = float4(0.0f, 1.0f, 0.0f, 1.0f); // Green
            }
            else
            {
                inout_color[pixel_coord] = float4(1.0f, 0.0f, 0.0f, 1.0f); // Red
            }
        }
    }
