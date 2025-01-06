#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"

RWStructuredBuffer<uint64_t> gaussian_keys_unsorted : register(u0); // ���ĵ��� ���� Ű �迭
RWStructuredBuffer<uint> gaussian_values_unsorted : register(u1);   // ���ĵ��� ���� �� �迭
StructuredBuffer<float2> points_xy : register(t0);                  // ����þ��� 2D ��ǥ
StructuredBuffer<float> depths : register(t1);                      // ����þ� ����
StructuredBuffer<int> radii : register(t2);                         // ����þ� ������
StructuredBuffer<uint> offsetTiles : register(t3);                  // Ÿ�Ϻ� ������
cbuffer PushConstants : register(b0) {
    GaussianPushConstants pushConstants;
}

[numthreads(256, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
    uint gaussianID = DTid;

    if (gaussianID >= pushConstants.num_gaussians) {
        return;
    }

    float2 position = points_xy[gaussianID];
    int radius = radii[gaussianID];

    uint2 rect_min, rect_max;

    rect_min.x = max(0, int((position.x - radius) / pushConstants.tileSize.x));
    rect_min.y = max(0, int((position.y - radius) / pushConstants.tileSize.y));

    rect_max.x = min(pushConstants.gridSize.x - 1, int((position.x + radius) / pushConstants.tileSize.x));
    rect_max.y = min(pushConstants.gridSize.y - 1, int((position.y + radius) / pushConstants.tileSize.y));

    uint offset = offsetTiles[gaussianID];

    for (uint y = rect_min.y; y <= rect_max.y; y++) {
        for (uint x = rect_min.x; x <= rect_max.x; x++) {
            uint tileID = y * pushConstants.gridSize.x + x;

            uint64_t key = uint64_t(tileID) << 32 | asuint(depths[gaussianID]);

            gaussian_keys_unsorted[offset] = key;
            gaussian_values_unsorted[offset] = gaussianID;

            offset++;
        }
    }
}
