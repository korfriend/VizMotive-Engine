#define OBJECTSHADER_COMPILE_PS
#define OBJECTSHADER_INPUT_POS
#define OBJECTSHADER_INPUT_TEX
#define OBJECTSHADER_INPUT_ATL
#define OBJECTSHADER_INPUT_COL
#define OBJECTSHADER_INPUT_TAN
#define OBJECTSHADER_USE_UVSETS
#define OBJECTSHADER_USE_COMMON
#define OBJECTSHADER_USE_COLOR
#define OBJECTSHADER_USE_NORMAL
#define OBJECTSHADER_USE_TANGENT
#define OBJECTSHADER_USE_EMISSIVE
#define ENVMAPRENDERING
#define FORWARD

// Advanced features includes
#include "../Globals.hlsli"
#include "../CommonHF/surfaceHF.hlsli"
#include "../CommonHF/brdf.hlsli"
#include "../CommonHF/lightingHF.hlsli"
#include "../CommonHF/shadingHF.hlsli"

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
    float2 atl : TEXCOORD1;
    float4 color : COLOR;
    float3 nor : NORMAL;
    float4 tan : TANGENT;
    float3 pos3D : WORLDPOSITION;
    uint instanceID : INSTANCEID;
};

float4 main(VertextoPixel input) : SV_Target
{
    ShaderMaterial material = GetMaterial();
    ShaderGeometry geometry = GetMesh();
    
    Surface surface;
    surface.init();
    surface.P = input.pos3D;
    surface.N = normalize(input.nor);
    surface.V = normalize(GetCamera().position - surface.P);
    
    // Tangent space calculations
    surface.T = half4(normalize(input.tan.xyz), input.tan.w);
    surface.B = normalize(cross(surface.N, surface.T.xyz) * surface.T.w);
    
    // UV coordinates
    float4 uvsets = float4(input.tex, input.atl);
    
    // Material properties
    float4 baseColor = unpack_rgba(material.baseColor);
    [branch]
    if (material.textures[BASECOLORMAP].IsValid())
    {
        float4 baseColorMap = material.textures[BASECOLORMAP].Sample(sampler_objectshader, uvsets);
        baseColor *= baseColorMap;
    }
    
    // Normal mapping
    [branch]
    if (material.textures[NORMALMAP].IsValid())
    {
        float3 normalMap = material.textures[NORMALMAP].Sample(sampler_objectshader, uvsets).rgb * 2 - 1;
        float3x3 TBN = float3x3(surface.T.xyz, surface.B, surface.N);
        surface.N = normalize(mul(normalMap, TBN));
    }
    
    // PBR material properties
    surface.albedo = baseColor.rgb * input.color.rgb;
    float roughness = unpack_rgba(material.roughness_reflectance_metalness_refraction).x;
    float metalness = unpack_rgba(material.roughness_reflectance_metalness_refraction).z;
    surface.f0 = lerp(0.04, baseColor.rgb, metalness);
    surface.roughness = roughness;
    surface.opacity = baseColor.a * input.color.a;
    
    // Advanced environment mapping with PBR
    float3 R = reflect(-surface.V, surface.N);
    float4 envColor = float4(0.2, 0.2, 0.3, 1.0); // Default sky color
    if (GetScene().globalenvmap >= 0)
    {
        float mipLevel = surface.roughness * 8.0; // Assume 8 mip levels
        envColor = bindless_cubemaps[descriptor_index(GetScene().globalenvmap)].SampleLevel(sampler_linear_clamp, R, mipLevel);
    }
    
    // Fresnel calculation
    float3 F = F_Schlick(surface.f0, saturate(dot(surface.N, surface.V)));
    
    // Environment contribution
    float3 environmentContribution = envColor.rgb * F;
    
    // Emissive
    float3 emissive = unpack_rgba(material.emissive_cloak).rgb;
    [branch]
    if (material.textures[EMISSIVEMAP].IsValid())
    {
        emissive *= material.textures[EMISSIVEMAP].Sample(sampler_objectshader, uvsets).rgb;
    }
    
    // Final color composition
    float3 finalColor = surface.albedo * (1.0 - F) + environmentContribution + emissive;
    
    return float4(finalColor, surface.opacity);
}