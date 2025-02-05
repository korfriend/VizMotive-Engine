#include "../Globals.hlsli"

static const uint THREADCOUNT = 64;

RWStructuredBuffer<ShaderMeshlet> output_meshlets : register(u0);

// this is for filling up the 'meshletBuffer' defined in 'GSceneDetails' class
[numthreads(1, THREADCOUNT, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
	uint geometryIndex = Gid.x;
    ShaderGeometry geometry = load_geometry(geometryIndex); // part
    for (uint j = groupIndex; j < geometry.meshletCount; j += THREADCOUNT)
    {
        ShaderMeshlet meshlet = (ShaderMeshlet)0;
        //meshlet.instanceIndex = instanceIndex;
        meshlet.geometryIndex = geometryIndex;

        if (geometry.vb_clu < 0)
        {
            meshlet.primitiveOffset = j * MESHLET_TRIANGLE_COUNT;
        }
        else
        {
            meshlet.primitiveOffset = (geometry.meshletOffset + j - geometry.meshletOffset) << 7u;
        }

        uint meshletIndex = geometry.meshletOffset + j;
        output_meshlets[meshletIndex] = meshlet;
    }
}
