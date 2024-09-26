#ifndef SHADERINTEROP_H
#define SHADERINTEROP_H

#ifdef __cplusplus // not invoking shader compiler, but included in engine source

// Application-side types:
#include "Libs/Math.h"

using float4x4 = XMFLOAT4X4;
using float2 = XMFLOAT2;
using float3 = XMFLOAT3;
using float4 = XMFLOAT4;
using uint = uint32_t;
using uint2 = XMUINT2;
using uint3 = XMUINT3;
using uint4 = XMUINT4;
using int2 = XMINT2;
using int3 = XMINT3;
using int4 = XMINT4;

#define column_major
#define row_major

#define CB_GETBINDSLOT(name) __CBUFFERBINDSLOT__##name##__
#define CBUFFER(name, slot) static const int CB_GETBINDSLOT(name) = slot; struct alignas(16) name
#define CONSTANTBUFFER(name, type, slot)
#define PUSHCONSTANT(name, type)

#else

// Shader - side types:

#define alignas(x)

#define PASTE1(a, b) a##b
#define PASTE(a, b) PASTE1(a, b)
#define CBUFFER(name, slot) cbuffer name : register(PASTE(b, slot))
#define CONSTANTBUFFER(name, type, slot) ConstantBuffer< type > name : register(PASTE(b, slot))

#if defined(__PSSL__)
// defined separately in preincluded PS5 extension file
#elif defined(__spirv__)
#define PUSHCONSTANT(name, type) [[vk::push_constant]] type name;
#else
#define PUSHCONSTANT(name, type) ConstantBuffer<type> name : register(b999)
#endif // __PSSL__

namespace vz
{
	namespace graphics
	{
		inline uint AlignTo(uint value, uint alignment)
		{
			return ((value + alignment - 1u) / alignment) * alignment;
		}
	}
}

#endif // __cplusplus

struct alignas(16) ShaderTransform
{
	float4 mat0;
	float4 mat1;
	float4 mat2;

	void Init()
	{
		mat0 = float4(1, 0, 0, 0);
		mat1 = float4(0, 1, 0, 0);
		mat2 = float4(0, 0, 1, 0);
	}
	void Create(float4x4 mat)
	{
		mat0 = float4(mat._11, mat._21, mat._31, mat._41);
		mat1 = float4(mat._12, mat._22, mat._32, mat._42);
		mat2 = float4(mat._13, mat._23, mat._33, mat._43);
	}
	float4x4 GetMatrix()
#ifdef __cplusplus
		const
#endif // __cplusplus
	{
		return float4x4(
			mat0.x, mat0.y, mat0.z, mat0.w,
			mat1.x, mat1.y, mat1.z, mat1.w,
			mat2.x, mat2.y, mat2.z, mat2.w,
			0, 0, 0, 1
		);
	}
};

struct alignas(16) ShaderMeshInstance
{
	uint uid;	// using entity
	uint flags;	// high 8 bits: user stencilRef (same as visibility-layered mask)
	uint indexGeometryBuffers;	// index of bindless array of VBs 
	uint color;

	uint countGeometries;		// number of all geometry parts
	int indexLightmap;			// lightmap index
	int indexAOBuffers;			// index of bindless array of AO VBs 
	uint alphaTest_size;

	float3 aabbCenter;
	float aabbRadius;

	float4 quaternion;
	ShaderTransform transform;
	ShaderTransform transformPrev;
	ShaderTransform transformRaw; // without quantization remapping applied

	void Init()
	{
		uid = 0;
		flags = 0;
		color = ~0u;
		indexLightmap = -1;
		indexGeometryBuffers = 0;
		countGeometries = 0;
		aabbCenter = float3(0, 0, 0);
		aabbRadius = 0;
		indexAOBuffers = -1;
		alphaTest_size = 0;
		quaternion = float4(0, 0, 0, 1);
		transform.Init();
		transformPrev.Init();
		transformRaw.Init();
	}

	inline void SetUserStencilRef(uint stencilRef)
	{
		flags |= (stencilRef & 0xFF) << 24u;
	}
	inline uint GetUserStencilRef()
	{
		return flags >> 24u;
	}

#ifndef __cplusplus
	inline half4 GetColor() { return (half4)unpack_rgba(color); }
	inline half3 GetEmissive() { return unpack_half3(emissive); }
	inline half GetAlphaTest() { return unpack_half2(alphaTest_size).x; }
	inline half GetSize() { return unpack_half2(alphaTest_size).y; }
#endif // __cplusplus
};

enum SHADERMATERIAL_OPTIONS
{
	SHADERMATERIAL_OPTION_BIT_USE_VERTEXCOLORS = 1 << 0,
	SHADERMATERIAL_OPTION_BIT_SPECULARGLOSSINESS_WORKFLOW = 1 << 1,
	SHADERMATERIAL_OPTION_BIT_OCCLUSION_PRIMARY = 1 << 2,
	SHADERMATERIAL_OPTION_BIT_OCCLUSION_SECONDARY = 1 << 3,
	SHADERMATERIAL_OPTION_BIT_USE_WIND = 1 << 4,
	SHADERMATERIAL_OPTION_BIT_RECEIVE_SHADOW = 1 << 5,
	SHADERMATERIAL_OPTION_BIT_CAST_SHADOW = 1 << 6,
	SHADERMATERIAL_OPTION_BIT_DOUBLE_SIDED = 1 << 7,
	SHADERMATERIAL_OPTION_BIT_TRANSPARENT = 1 << 8,
	SHADERMATERIAL_OPTION_BIT_ADDITIVE = 1 << 9,
	SHADERMATERIAL_OPTION_BIT_UNLIT = 1 << 10,
	SHADERMATERIAL_OPTION_BIT_USE_VERTEXAO = 1 << 11,
};

// Same as MaterialComponent::TextureSlot
enum TEXTURESLOT
{
	BASECOLORMAP,
	VOLUMEDENSITYMAP, // this is used for volume rendering

	TEXTURESLOT_COUNT
};

struct alignas(16) ShaderTextureSlot
{
	uint uvset_lodclamp;
	int texture_descriptor;
	int sparse_residencymap_descriptor;
	int sparse_feedbackmap_descriptor;

	inline void Init()
	{
		uvset_lodclamp = 0;
		texture_descriptor = -1;
		sparse_residencymap_descriptor = -1;
		sparse_feedbackmap_descriptor = -1;
	}

	inline bool IsValid()
	{
		return texture_descriptor >= 0;
	}
	inline uint GetUVSet()
	{
		return uvset_lodclamp & 1u;
	}

#ifndef __cplusplus
	inline float GetLodClamp()
	{
		return f16tof32((uvset_lodclamp >> 1u) & 0xFFFF);
	}
	Texture2D GetTexture()
	{
		return bindless_textures[UniformTextureSlot(texture_descriptor)];
	}
	float4 SampleVirtual(
		in Texture2D tex,
		in SamplerState sam,
		in float2 uv,
		in Texture2D<uint4> residency_map,
		in uint2 virtual_tile_count,
		in uint2 virtual_image_dim,
		in float virtual_lod
	)
	{
		virtual_lod = max(0, virtual_lod);

#ifdef SVT_FEEDBACK
		[branch]
			if (sparse_feedbackmap_descriptor >= 0)
			{
				RWTexture2D<uint> feedback_map = bindless_rwtextures_uint[UniformTextureSlot(sparse_feedbackmap_descriptor)];
				uint2 pixel = uv * virtual_tile_count;
				InterlockedMin(feedback_map[pixel], uint(virtual_lod));
			}
#endif // SVT_FEEDBACK

		float2 atlas_dim;
		tex.GetDimensions(atlas_dim.x, atlas_dim.y);
		const float2 atlas_dim_rcp = rcp(atlas_dim);

		const uint max_nonpacked_lod = uint(GetLodClamp());
		virtual_lod = min(virtual_lod, max_nonpacked_lod + SVT_PACKED_MIP_COUNT);
		bool packed_mips = uint(virtual_lod) > max_nonpacked_lod;

		uint2 pixel = uv * virtual_tile_count;
		uint4 residency = residency_map.Load(uint3(pixel >> uint(virtual_lod), min(max_nonpacked_lod, uint(virtual_lod))));
		uint2 tile = packed_mips ? residency.zw : residency.xy;
		const float clamped_lod = virtual_lod < max_nonpacked_lod ? max(virtual_lod, residency.z) : virtual_lod;

		// Mip - more detailed:
		float4 value0;
		{
			uint lod0 = uint(clamped_lod);
			const uint packed_mip_idx = packed_mips ? uint(virtual_lod - max_nonpacked_lod - 1) : 0;
			uint2 tile_pixel_upperleft = tile * SVT_TILE_SIZE_PADDED + SVT_TILE_BORDER + SVT_PACKED_MIP_OFFSETS[packed_mip_idx];
			uint2 virtual_lod_dim = max(4u, virtual_image_dim >> lod0);
			float2 virtual_pixel = uv * virtual_lod_dim;
			float2 virtual_tile_pixel = fmod(virtual_pixel, SVT_TILE_SIZE);
			float2 atlas_tile_pixel = tile_pixel_upperleft + 0.5 + virtual_tile_pixel;
			float2 atlas_uv = atlas_tile_pixel * atlas_dim_rcp;
			value0 = tex.SampleLevel(sam, atlas_uv, 0);
		}

		// Mip - less detailed:
		float4 value1;
		{
			uint lod1 = uint(clamped_lod + 1);
			packed_mips = uint(lod1) > max_nonpacked_lod;
			const uint packed_mip_idx = packed_mips ? uint(lod1 - max_nonpacked_lod - 1) : 0;
			residency = residency_map.Load(uint3(pixel >> lod1, min(max_nonpacked_lod, lod1)));
			tile = packed_mips ? residency.zw : residency.xy;
			uint2 tile_pixel_upperleft = tile * SVT_TILE_SIZE_PADDED + SVT_TILE_BORDER + SVT_PACKED_MIP_OFFSETS[packed_mip_idx];
			uint2 virtual_lod_dim = max(4u, virtual_image_dim >> lod1);
			float2 virtual_pixel = uv * virtual_lod_dim;
			float2 virtual_tile_pixel = fmod(virtual_pixel, SVT_TILE_SIZE);
			float2 atlas_tile_pixel = tile_pixel_upperleft + 0.5 + virtual_tile_pixel;
			float2 atlas_uv = atlas_tile_pixel * atlas_dim_rcp;
			value1 = tex.SampleLevel(sam, atlas_uv, 0);
		}

		return lerp(value0, value1, frac(clamped_lod)); // custom trilinear filtering
	}
	float4 Sample(in SamplerState sam, in float4 uvsets)
	{
		Texture2D tex = GetTexture();
		float2 uv = GetUVSet() == 0 ? uvsets.xy : uvsets.zw;

#ifndef DISABLE_SVT
		[branch]
			if (sparse_residencymap_descriptor >= 0)
			{
				Texture2D<uint4> residency_map = bindless_textures_uint4[UniformTextureSlot(sparse_residencymap_descriptor)];
				float2 virtual_tile_count;
				residency_map.GetDimensions(virtual_tile_count.x, virtual_tile_count.y);
				float2 virtual_image_dim = virtual_tile_count * SVT_TILE_SIZE;
				float virtual_lod = get_lod(virtual_image_dim, ddx_coarse(uv), ddy_coarse(uv));
				return SampleVirtual(tex, sam, uv, residency_map, virtual_tile_count, virtual_image_dim, virtual_lod);
			}
#endif // DISABLE_SVT

		return tex.Sample(sam, uv);
	}

	float4 SampleLevel(in SamplerState sam, in float4 uvsets, in float lod)
	{
		Texture2D tex = GetTexture();
		float2 uv = GetUVSet() == 0 ? uvsets.xy : uvsets.zw;

#ifndef DISABLE_SVT
		[branch]
			if (sparse_residencymap_descriptor >= 0)
			{
				Texture2D<uint4> residency_map = bindless_textures_uint4[UniformTextureSlot(sparse_residencymap_descriptor)];
				float2 virtual_tile_count;
				residency_map.GetDimensions(virtual_tile_count.x, virtual_tile_count.y);
				float2 virtual_image_dim = virtual_tile_count * SVT_TILE_SIZE;
				return SampleVirtual(tex, sam, uv, residency_map, virtual_tile_count, virtual_image_dim, lod);
			}
#endif // DISABLE_SVT

		return tex.SampleLevel(sam, uv, lod);
	}

	float4 SampleBias(in SamplerState sam, in float4 uvsets, in float bias)
	{
		Texture2D tex = GetTexture();
		float2 uv = GetUVSet() == 0 ? uvsets.xy : uvsets.zw;

#ifndef DISABLE_SVT
		[branch]
			if (sparse_residencymap_descriptor >= 0)
			{
				Texture2D<uint4> residency_map = bindless_textures_uint4[UniformTextureSlot(sparse_residencymap_descriptor)];
				float2 virtual_tile_count;
				residency_map.GetDimensions(virtual_tile_count.x, virtual_tile_count.y);
				float2 virtual_image_dim = virtual_tile_count * SVT_TILE_SIZE;
				float virtual_lod = get_lod(virtual_image_dim, ddx_coarse(uv), ddy_coarse(uv));
				virtual_lod += bias;
				return SampleVirtual(tex, sam, uv, residency_map, virtual_tile_count, virtual_image_dim, virtual_lod + bias);
			}
#endif // DISABLE_SVT

		return tex.SampleBias(sam, uv, bias);
	}

	float4 SampleGrad(in SamplerState sam, in float4 uvsets, in float4 uvsets_dx, in float4 uvsets_dy)
	{
		Texture2D tex = GetTexture();
		float2 uv = GetUVSet() == 0 ? uvsets.xy : uvsets.zw;
		float2 uv_dx = GetUVSet() == 0 ? uvsets_dx.xy : uvsets_dx.zw;
		float2 uv_dy = GetUVSet() == 0 ? uvsets_dy.xy : uvsets_dy.zw;

#ifndef DISABLE_SVT
		[branch]
			if (sparse_residencymap_descriptor >= 0)
			{
				Texture2D<uint4> residency_map = bindless_textures_uint4[UniformTextureSlot(sparse_residencymap_descriptor)];
				float2 virtual_tile_count;
				residency_map.GetDimensions(virtual_tile_count.x, virtual_tile_count.y);
				float2 virtual_image_dim = virtual_tile_count * SVT_TILE_SIZE;
				float virtual_lod = get_lod(virtual_image_dim, uv_dx, uv_dy);
				return SampleVirtual(tex, sam, uv, residency_map, virtual_tile_count, virtual_image_dim, virtual_lod);
			}
#endif // DISABLE_SVT

		return tex.SampleGrad(sam, uv, uv_dx, uv_dy);
	}
#endif // __cplusplus
};

struct alignas(16) ShaderMaterial
{
	uint2 baseColor;
	uint2 normalmap_pom_alphatest_displacement;

	uint2 roughness_reflectance_metalness_refraction;
	uint2 emissive_cloak;

	uint2 subsurfaceScattering;
	uint2 subsurfaceScattering_inv;

	uint2 specular_chromatic;
	uint2 sheenColor;

	float4 texMulAdd;

	uint2 transmission_sheenroughness_clearcoat_clearcoatroughness;
	uint2 aniso_anisosin_anisocos_terrainblend;

	int sampler_descriptor;
	uint options_stencilref;
	uint layerMask;
	uint shaderType;

	uint4 userdata;

	ShaderTextureSlot textures[TEXTURESLOT_COUNT];

	void Init()
	{
		baseColor = uint2(0, 0);
		normalmap_pom_alphatest_displacement = uint2(0, 0);

		roughness_reflectance_metalness_refraction = uint2(0, 0);
		emissive_cloak = uint2(0, 0);

		subsurfaceScattering = uint2(0, 0);
		subsurfaceScattering_inv = uint2(0, 0);

		specular_chromatic = uint2(0, 0);
		sheenColor = uint2(0, 0);

		texMulAdd = float4(1, 1, 0, 0);

		transmission_sheenroughness_clearcoat_clearcoatroughness = uint2(0, 0);
		aniso_anisosin_anisocos_terrainblend = uint2(0, 0);

		sampler_descriptor = -1;
		options_stencilref = 0;
		layerMask = ~0u;
		shaderType = 0;

		userdata = uint4(0, 0, 0, 0);

		for (int i = 0; i < TEXTURESLOT_COUNT; ++i)
		{
			textures[i].Init();
		}
	}

#ifndef __cplusplus
	inline half4 GetBaseColor() { return unpack_half4(baseColor); }
	inline half4 GetSSS() { return unpack_half4(subsurfaceScattering); }
	inline half4 GetSSSInverse() { return unpack_half4(subsurfaceScattering_inv); }
	inline half3 GetEmissive() { return unpack_half4(emissive_cloak).rgb; }
	inline half GetCloak() { return unpack_half4(emissive_cloak).a; }
	inline half3 GetSpecular() { return unpack_half3(specular_chromatic); }
	inline half GetChromaticAberration() { return unpack_half4(specular_chromatic).w; }
	inline half3 GetSheenColor() { return unpack_half3(sheenColor); }
	inline half GetRoughness() { return unpack_half4(roughness_reflectance_metalness_refraction).x; }
	inline half GetReflectance() { return unpack_half4(roughness_reflectance_metalness_refraction).y; }
	inline half GetMetalness() { return unpack_half4(roughness_reflectance_metalness_refraction).z; }
	inline half GetRefraction() { return unpack_half4(roughness_reflectance_metalness_refraction).w; }
	inline half GetNormalMapStrength() { return unpack_half4(normalmap_pom_alphatest_displacement).x; }
	inline half GetParallaxOcclusionMapping() { return unpack_half4(normalmap_pom_alphatest_displacement).y; }
	inline half GetAlphaTest() { return unpack_half4(normalmap_pom_alphatest_displacement).z; }
	inline half GetDisplacement() { return unpack_half4(normalmap_pom_alphatest_displacement).w; }
	inline half GetTransmission() { return unpack_half4(transmission_sheenroughness_clearcoat_clearcoatroughness).x; }
	inline half GetSheenRoughness() { return unpack_half4(transmission_sheenroughness_clearcoat_clearcoatroughness).y; }
	inline half GetClearcoat() { return unpack_half4(transmission_sheenroughness_clearcoat_clearcoatroughness).z; }
	inline half GetClearcoatRoughness() { return unpack_half4(transmission_sheenroughness_clearcoat_clearcoatroughness).w; }
	inline half GetAnisotropy() { return unpack_half4(aniso_anisosin_anisocos_terrainblend).x; }
	inline half GetAnisotropySin() { return unpack_half4(aniso_anisosin_anisocos_terrainblend).y; }
	inline half GetAnisotropyCos() { return unpack_half4(aniso_anisosin_anisocos_terrainblend).z; }
	inline half GetTerrainBlendRcp() { return unpack_half4(aniso_anisosin_anisocos_terrainblend).w; }
	inline uint GetStencilRef() { return options_stencilref >> 24u; }
#endif // __cplusplus

	inline uint GetOptions() { return options_stencilref; }
	inline bool IsUsingVertexColors() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_USE_VERTEXCOLORS; }
	inline bool IsUsingVertexAO() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_USE_VERTEXAO; }
	inline bool IsUsingSpecularGlossinessWorkflow() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_SPECULARGLOSSINESS_WORKFLOW; }
	inline bool IsOcclusionEnabled_Primary() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_OCCLUSION_PRIMARY; }
	inline bool IsOcclusionEnabled_Secondary() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_OCCLUSION_SECONDARY; }
	inline bool IsUsingWind() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_USE_WIND; }
	inline bool IsReceiveShadow() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_RECEIVE_SHADOW; }
	inline bool IsCastingShadow() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_CAST_SHADOW; }
	inline bool IsUnlit() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_UNLIT; }
	inline bool IsTransparent() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_TRANSPARENT; }
	inline bool IsAdditive() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_ADDITIVE; }
	inline bool IsDoubleSided() { return GetOptions() & SHADERMATERIAL_OPTION_BIT_DOUBLE_SIDED; }
};


#endif