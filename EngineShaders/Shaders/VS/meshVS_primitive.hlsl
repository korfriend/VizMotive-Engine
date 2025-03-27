#include "../Globals.hlsli"
#include "../CommonHF/objectHF.hlsli"

struct VertexToPixel
{
	float4 pos : SV_POSITION;
	float4 col : COLOR;
};

inline float4 Unpack_8bitUIntRGBA(in uint value)
{
	float4 color;
	color.x = (float)((value >> 0u) & 0xFF) / 255.0;
	color.y = (float)((value >> 8u) & 0xFF) / 255.0;
	color.z = (float)((value >> 16u) & 0xFF) / 255.0;
	color.w = (float)((value >> 24u) & 0xFF) / 255.0;
	return color;
}


ShaderMeshInstancePointer GetInstancePointer()
{
	const uint instanceID = 0;
	if (push.instances >= 0)
		return bindless_buffers[descriptor_index(push.instances)].Load<ShaderMeshInstancePointer>(push.instance_offset + instanceID * sizeof(ShaderMeshInstancePointer));

	ShaderMeshInstancePointer poi;
	poi.Init();
	return poi;
}

ShaderMeshInstance GetInstance()
{
	if (push.instances >= 0)
		return load_instance(GetInstancePointer().GetInstanceIndex());

	ShaderMeshInstance inst;
	inst.Init();
	return inst;
}

float4 GetPositionWind(uint vertexID)
{
	return bindless_buffers_float4[descriptor_index(GetMesh().vb_pos_w)][vertexID];
}

VertexToPixel main(uint vI : SV_VertexID)
{
	VertexToPixel Out;
	
	float4 position = GetPositionWind(vI);
	position.w = 1.f;
	//ShaderMeshInstance inst = GetInstance();
	//ShaderCamera camera = GetCamera();
	//Out.pos = mul(inst.transform.GetMatrix(), position);
	//Out.pos = mul(camera.view_projection, Out.pos);
	Out.pos = mul(g_xTransform, position);
	uint color_uint = bindless_buffers[descriptor_index(GetMesh().vb_col)].Load<uint>(vI * sizeof(uint));
	Out.col = Unpack_8bitUIntRGBA(color_uint);

	return Out;
}

