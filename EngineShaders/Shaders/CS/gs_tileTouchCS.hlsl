#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"

RWStructuredBuffer<uint> touchedTiles : register(u0); // Ÿ�Ϻ� ��ġ ī��Ʈ
StructuredBuffer<float2> points_xy : register(t0);   // ����þ��� 2D ��ǥ
StructuredBuffer<int> radii : register(t1);          // ����þ� ������
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

    for (uint y = rect_min.y; y <= rect_max.y; y++) {
        for (uint x = rect_min.x; x <= rect_max.x; x++) {
            uint tileIndex = y * pushConstants.gridSize.x + x;
            InterlockedAdd(touchedTiles[tileIndex], 1);
        }
    }
}
