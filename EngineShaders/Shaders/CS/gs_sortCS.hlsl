#include "../Globals.hlsli"
#include "../ShaderInterop_GaussianSplatting.h"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/raytracingHF.hlsli"   

#define WORKGROUP_SIZE 256   // Must be >= RADIX_SORT_BINS
#define RADIX_SORT_BINS 256u
#define SUBGROUP_SIZE 32     // 32 or 64 typically
#define BITS 64

PUSHCONSTANT(radix_constants, GaussianRadixConstants);

//cbuffer PushConstants : register(b0)
//{
//    uint g_num_elements;
//    uint g_shift;
//    uint g_num_workgroups;
//    uint g_num_blocks_per_workgroup;
//};

RWStructuredBuffer<uint64_t> g_elements_in : register(u0);
RWStructuredBuffer<uint64_t> g_elements_out : register(u1);
RWStructuredBuffer<uint> g_payload_in : register(u2);
RWStructuredBuffer<uint> g_payload_out : register(u3);

RWStructuredBuffer<uint> g_histograms : register(u4);

// For partial sums across subgroups
groupshared uint sums[RADIX_SORT_BINS / SUBGROUP_SIZE];

// For global prefix sums of each bin
groupshared uint global_offsets[RADIX_SORT_BINS];

// Instead of 64-bit flags, we use an array of 32-bit words per bin
// for each of the 256 threads in the group => 256 bits => 8 x 32
struct BinFlags
{
    uint flags[WORKGROUP_SIZE / 32];
};
groupshared BinFlags bin_flags[RADIX_SORT_BINS];

uint WaveSubgroupAdd(uint val)
{
    return WaveActiveSum(val);
}

uint WaveSubgroupExclusiveAdd(uint val)
{
    uint inclusiveSum = WavePrefixSum(val); // inclusive
    return inclusiveSum - val;              // make it exclusive
}

bool WaveSubgroupElect()
{
    return WaveIsFirstLane();
}

uint WaveSubgroupBroadcast(uint val, uint srcLane)
{
    return WaveReadLaneAt(val, srcLane);
}

[numthreads(WORKGROUP_SIZE, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
    uint g_num_elements = radix_constants.g_num_elements;
    uint g_shift = radix_constants.g_shift;
    uint g_num_workgroups = radix_constants.g_num_workgroups;
    uint g_num_blocks_per_workgroup = radix_constants.g_num_blocks_per_workgroup;

    uint gID = DTid.x; // global thread ID
    uint lID = GTid.x; // local thread ID
    uint wID = Gid.x;  // workgroup ID

    // We do not have gl_SubgroupID or gl_SubgroupInvocationID in HLSL directly.
    // We'll compute them manually:
    uint sID = lID / SUBGROUP_SIZE; // which subgroup within the thread group
    uint lsID = lID % SUBGROUP_SIZE; // lane ID within that subgroup

    uint local_histogram = 0;
    uint prefix_sum = 0;
    uint histogram_count = 0;

    if (lID < RADIX_SORT_BINS)
    {
        // Accumulate total histogram count for bin lID across all WGs
        uint count = 0;
        for (uint j = 0; j < g_num_workgroups; j++)
        {
            uint t = g_histograms[RADIX_SORT_BINS * j + lID];
            if (j == wID)
            {
                // partial sum of all preceding WGs for bin lID
                local_histogram = count;
            }
            count += t;
        }
        histogram_count = count;

        // Subgroup (wave) operations
        uint sum = WaveSubgroupAdd(histogram_count);
        prefix_sum = WaveSubgroupExclusiveAdd(histogram_count);

        // Execute once per wave
        if (WaveSubgroupElect())
        {
            sums[sID] = sum;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    // Next, prefix sum across sums[]
    if (lID < RADIX_SORT_BINS)
    {
        //uint sums_inclusive = WavePrefixSum(sums[lsID]);
        //uint sums_exclusive = sums_inclusive - sums[lsID]; // exclusive
        //uint sums_prefix_sum = WaveSubgroupBroadcast(sums_exclusive, sID);
        uint sums_inclusive = WavePrefixSum(sums[sID]);
        uint sums_exclusive = sums_inclusive - sums[sID]; // exclusive
        uint sums_prefix_sum = WaveSubgroupBroadcast(sums_exclusive, sID);

        // Combine wave prefix + local prefix + partial from previous WGs
        uint global_histogram = sums_prefix_sum + prefix_sum;
        global_offsets[lID] = global_histogram + local_histogram;
    }
    GroupMemoryBarrierWithGroupSync();

    for (uint index = 0; index < g_num_blocks_per_workgroup; index++)
    {
        uint elementId = wID * g_num_blocks_per_workgroup * WORKGROUP_SIZE
                       + index * WORKGROUP_SIZE
                       + lID;

        // Initialize bin_flags to 0 (only do so for lID < RADIX_SORT_BINS)
        if (lID < RADIX_SORT_BINS)
        {
            // 256 threads => 8 x 32 bits
            for (uint i = 0; i < (WORKGROUP_SIZE / 32); i++)
            {
                bin_flags[lID].flags[i] = 0u;
            }
        }
        GroupMemoryBarrierWithGroupSync();

        // Load the input key/payload, compute bin
        uint64_t element_in = 0;
        uint payload_val = 0;
        uint binID = 0;
        uint binOffset = 0;

        if (elementId < g_num_elements)
        {
            element_in = g_elements_in[elementId];
            payload_val = g_payload_in[elementId];
            binID = (uint) (element_in >> g_shift) & (RADIX_SORT_BINS - 1);

            binOffset = global_offsets[binID];

            // Instead of 64-bit atomics, set the appropriate bit in a 32-bit word
            uint flags_bin = lID / 32;
            uint flags_bit = 1u << (lID % 32);

            InterlockedAdd(bin_flags[binID].flags[flags_bin], flags_bit);
        }
        GroupMemoryBarrierWithGroupSync();

        if (elementId < g_num_elements)
        {
            // Count how many bits are set before our bit => prefix
            uint prefix = 0;
            uint count = 0;

            for (uint i = 0; i < (WORKGROUP_SIZE / 32); i++)
            {
                uint bits = bin_flags[binID].flags[i];

                // total bits in this 32-bit chunk
                uint full_count = countbits(bits);

                // partial bits for the chunk if i == flags_bin
                uint partial_mask = (1u << (lID % 32)) - 1u;
                uint partial_bits = bits & partial_mask;
                uint partial_count = countbits(partial_bits);

                // accumulate prefix
                if (i < (lID / 32))
                {
                    prefix += full_count;
                }
                else if (i == (lID / 32))
                {
                    prefix += partial_count;
                }

                count += full_count;
            }

            // Write out the key and payload
            g_elements_out[binOffset + prefix] = element_in;
            g_payload_out[binOffset + prefix] = payload_val;

            // If we're the last element for this bin, increment global_offsets
            if (prefix == (count - 1))
            {
                // Still a 32-bit InterlockedAdd to increment the offset
                InterlockedAdd(global_offsets[binID], count);
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
