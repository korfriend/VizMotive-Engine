#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"

// Define the same constants
#define WORKGROUP_SIZE 256
#define RADIX_SORT_BINS 256u
#define BITS 64

PUSHCONSTANT(radix_constants, GaussianRadixConstants);

//cbuffer PushConstants : register(b0)
//{
//    uint g_num_elements;
//    uint g_shift;
//    uint g_num_workgroups;
//    uint g_num_blocks_per_workgroup;
//};

// Input buffer (keys/elements)
RWStructuredBuffer<uint64_t> g_elements_in : register(u0);

// Histogram buffer
// - The size is RADIX_SORT_BINS * g_num_workgroups
RWStructuredBuffer<uint> g_histograms : register(u1);

groupshared uint histogram[RADIX_SORT_BINS]; // Group-shared memory (similar to 'shared' in GLSL)

[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(
    uint3 dispatchThreadID : SV_DispatchThreadID,   // Global thread ID (gl_GlobalInvocationID)
    uint3 groupThreadID : SV_GroupThreadID,         // Local thread ID (gl_LocalInvocationID)
    uint3 groupID : SV_GroupID                      // Workgroup ID (gl_WorkGroupID)
)
{

    uint gID = dispatchThreadID.x;
    uint lID = groupThreadID.x;
    uint wID = groupID.x;

    // 1. Initialize the shared histogram
    if (lID < RADIX_SORT_BINS)
    {
        histogram[lID] = 0;
    }

    // Make sure all threads see the zeroed histogram
    GroupMemoryBarrierWithGroupSync();

    // 2. Accumulate histogram counts
    for (uint index = 0; index < radix_constants.g_num_blocks_per_workgroup; index++)
    {
        uint elementId = wID * radix_constants.g_num_blocks_per_workgroup * WORKGROUP_SIZE
                       + index * WORKGROUP_SIZE
                       + lID;

        if (elementId < radix_constants.g_num_elements)
        {
            // Determine the bin
            uint bin = uint(g_elements_in[elementId] >> radix_constants.g_shift) & (RADIX_SORT_BINS - 1);
            // Increment local histogram
            InterlockedAdd(histogram[bin], 1);
        }
    }

    // Synchronize again
    GroupMemoryBarrierWithGroupSync();

    // 3. Store the local histogram into the global histogram buffer
    if (lID < RADIX_SORT_BINS)
    {
        // The offset for this workgroup’s histogram
        uint histOffset = RADIX_SORT_BINS * wID + lID;
        g_histograms[histOffset] = histogram[lID];
    }
}
