#define OBJECTSHADER_COMPILE_PS
#define OBJECTSHADER_INPUT_POS
#define OBJECTSHADER_INPUT_TEX
#define ENVMAPRENDERING
#define FORWARD

// Minimal includes for basic environment mapping
#include "../Globals.hlsli"

PUSHCONSTANT(push, MeshPushConstants);

inline ShaderGeometry GetMesh()
{
    return load_geometry(push.geometryIndex);
}

inline ShaderMaterial GetMaterial()
{
    return load_material(push.materialIndex);
}

#define sampler_objectshader bindless_samplers[descriptor_index(GetMaterial().sampler_descriptor)]

struct VertextoPixel
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
    float3 nor : NORMAL;
    float3 pos3D : WORLDPOSITION;
};

float4 main(VertextoPixel input) : SV_Target
{
    ShaderMaterial material = GetMaterial();
    
    // Basic environment mapping calculation
    float3 viewDir = normalize(GetCamera().position - input.pos3D);
    float3 normal = normalize(input.nor);
    float3 reflectionDir = reflect(-viewDir, normal);
    
    // Sample environment map
    float4 envColor = float4(0.2, 0.2, 0.3, 1.0); // Default sky color
    if (GetScene().globalenvmap >= 0)
    {
        envColor = bindless_cubemaps[descriptor_index(GetScene().globalenvmap)].SampleLevel(sampler_linear_clamp, reflectionDir, 0);
    }
    
    // Basic material color
    float4 baseColor = float4(1, 1, 1, 1);
    if (material.textures[BASECOLORMAP].IsValid())
    {
        baseColor = material.textures[BASECOLORMAP].Sample(sampler_objectshader, float4(input.tex, 0, 0));
    }
    
    // Simple blend
    float4 finalColor = lerp(baseColor, envColor, 0.3);
    
    return finalColor;
}