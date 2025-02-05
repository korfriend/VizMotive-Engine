#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   
[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
}
//cbuffer Constants : register(b0)
//{
//    uint tileX;
//};
//
//struct VertexAttribute
//{
//    float4 color_radii;
//    float4 aabb;
//    float  depth;
//};
//
//StructuredBuffer<VertexAttribute> Vertices    : register(t0);
//StructuredBuffer<uint>           prefixSum   : register(t1);
//
//RWStructuredBuffer<uint64_t>     OutKeys     : register(u0);
//RWStructuredBuffer<uint>         OutPayloads : register(u1);
//
//[numthreads(256, 1, 1)]
//void main(uint3 DTid : SV_DispatchThreadID)
//{
//    uint index = DTid.x;
//
//    if (index >= prefixSum.Length())
//        return;
//
//    if (Vertices[index].color_radii.w == 0)
//        return;
//
//    uint ind = (index == 0) ? 0 : prefixSum[index - 1];
//
//    for (uint i = (uint)Vertices[index].aabb.x; i < (uint)Vertices[index].aabb.z; i++)
//    {
//        for (uint j = (uint)Vertices[index].aabb.y; j < (uint)Vertices[index].aabb.w; j++)
//        {
//            uint64_t tileIndex = ((uint64_t)i) + ((uint64_t)j * tileX);
//            uint depthBits = asuint(Vertices[index].depth);
//            uint64_t k = (tileIndex << 32) | ((uint64_t)depthBits);
//            OutKeys[ind] = k;
//            OutPayloads[ind] = index;
//            ind++;
//        }
//    }
//}
