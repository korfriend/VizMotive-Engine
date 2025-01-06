#include "../Globals.hlsli"
#include "../ShaderInterop_GS.h"

RWStructuredBuffer<uint> offsetTiles : register(u0);  // 출력: 오프셋 배열
StructuredBuffer<uint> touchedTiles : register(t0);   // 입력: 터치된 타일 카운트
cbuffer PushConstants : register(b0) {
    GaussianPushConstants pushConstants;
}

[numthreads(256, 1, 1)]
void main(uint DTid : SV_DispatchThreadID)
{
    uint tileID = DTid;

    uint totalTiles = pushConstants.gridSize.x * pushConstants.gridSize.y;

    if (tileID >= totalTiles) {
        return;
    }

    if (tileID == 0) {
        offsetTiles[tileID] = 0;
    }
    else {
        offsetTiles[tileID] = offsetTiles[tileID - 1] + touchedTiles[tileID - 1];
    }
}
