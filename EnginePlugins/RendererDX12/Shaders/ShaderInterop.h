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

#define CBSLOT_RENDERER_FRAME	0
#define CBSLOT_RENDERER_CAMERA	1

static const uint TILED_CULLING_BLOCKSIZE = 16;
static const uint TILED_CULLING_THREADSIZE = 8;
static const uint TILED_CULLING_GRANULARITY = TILED_CULLING_BLOCKSIZE / TILED_CULLING_THREADSIZE;

static const uint VISIBILITY_BLOCKSIZE = 8;
static const uint VISIBILITY_TILED_CULLING_GRANULARITY = TILED_CULLING_BLOCKSIZE / VISIBILITY_BLOCKSIZE;

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

struct alignas(16) ShaderRenderable
{
	uint uid;	// using entity
	uint flags;	// high 8 bits: user stencilRef (same as visibility-layered mask)
	uint alphaTest_size;
	float fadeDistance;

	uint geometryOffset; // offset of all geometries for currently active LOD (geomtryPartIndex applied by LODs)
	uint geometryCount;
	uint baseGeometryOffset;	// offset of all geometries of the instance (if no LODs, then it is equal to geometryOffset)
	uint baseGeometryCount;

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
		alphaTest_size = 0;
		fadeDistance = 0;

		geometryOffset = baseGeometryOffset = 0;
		baseGeometryOffset = baseGeometryOffset = 0;

		aabbCenter = float3(0, 0, 0);
		aabbRadius = 0;

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

enum FRAME_OPTIONS
{
	OPTION_BIT_TEMPORALAA_ENABLED = 1 << 0,
	//OPTION_BIT_TRANSPARENTSHADOWS_ENABLED = 1 << 1,
	//OPTION_BIT_VXGI_ENABLED = 1 << 2,
	//OPTION_BIT_VXGI_REFLECTIONS_ENABLED = 1 << 3,
	//OPTION_BIT_REALISTIC_SKY = 1 << 6,
	//OPTION_BIT_HEIGHT_FOG = 1 << 7,
	//OPTION_BIT_RAYTRACED_SHADOWS = 1 << 8,
	//OPTION_BIT_SHADOW_MASK = 1 << 9,
	//OPTION_BIT_SURFELGI_ENABLED = 1 << 10,
	//OPTION_BIT_DISABLE_ALBEDO_MAPS = 1 << 11,
	//OPTION_BIT_FORCE_DIFFUSE_LIGHTING = 1 << 12,
	//OPTION_BIT_VOLUMETRICCLOUDS_CAST_SHADOW = 1 << 13,
	//OPTION_BIT_OVERRIDE_FOG_COLOR = 1 << 14,
	//OPTION_BIT_STATIC_SKY_SPHEREMAP = 1 << 15,
	//OPTION_BIT_REALISTIC_SKY_AERIAL_PERSPECTIVE = 1 << 16,
	//OPTION_BIT_REALISTIC_SKY_HIGH_QUALITY = 1 << 17,
	//OPTION_BIT_REALISTIC_SKY_RECEIVE_SHADOW = 1 << 18,
	//OPTION_BIT_VOLUMETRICCLOUDS_RECEIVE_SHADOW = 1 << 19,
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

// This is equivalent to a Geometry's Primitive parts
//	But because these are always loaded together by shaders, they are unrolled into one to reduce individual buffer loads
struct alignas(16) ShaderGeometryPart
{
	int ib;
	int vb_pos;
	int vb_uvs;
	int vb_nor;

	int vb_tan;
	int vb_col;
	int vb_pre;
	uint padding0;

	float3 aabb_min;
	uint flags;

	float3 aabb_max;
	float tessellation_factor;	

	float2 uv_range_min;
	float2 uv_range_max;

	void Init()
	{
		ib = -1;
		vb_pos = -1;
		vb_uvs = -1;
		vb_nor = -1;
		vb_tan = -1;
		vb_col = -1;
		vb_pre = -1;

		aabb_min = float3(0, 0, 0);
		flags = 0;
		aabb_max = float3(0, 0, 0);
		tessellation_factor = 0;

		uv_range_min = float2(0, 0);
		uv_range_max = float2(1, 1);
	}
};

// buffer's element (not constant buffer)
struct ShaderMeshInstancePointer
{
	uint data;

	void Init()
	{
		data = 0;
	}
	void Create(uint _instanceIndex, uint camera_index = 0, float dither = 0)
	{
		data = 0;
		data |= _instanceIndex & 0xFFFFFF;
		data |= (camera_index & 0xF) << 24u;
		data |= (uint(dither * 15.0f) & 0xF) << 28u;
	}
	uint GetInstanceIndex()
	{
		return data & 0xFFFFFF;
	}
	uint GetCameraIndex()
	{
		return (data >> 24u) & 0xF;
	}
	float GetDither()
	{
		return float((data >> 28u) & 0xF) / 15.0f;
	}
};

struct RenderablePushConstants
{
	uint geometryIndex;
	uint materialIndex;
	int instances;
	uint instance_offset;
};

struct alignas(16) ShaderScene
{
	int instancebuffer;
	int geometrybuffer;
	int materialbuffer;
	int texturestreamingbuffer;
	
	// TODO
	int TLAS;
	int BVH_counter;
	int BVH_nodes;
	int BVH_primitives;

	float3 aabb_min;
	int globalenvmap; // static sky, not guaranteed to be cubemap, mipmaps or format, just whatever is imported
	float3 aabb_max;
	int globalprobe; // rendered probe with guaranteed mipmaps, hdr, etc.
	float3 aabb_extents;		// enclosing AABB abs(max - min)
	float padding5;
	float3 aabb_extents_rcp;	// enclosing AABB 1.0f / abs(max - min)
	float padding6;

	// TODO
	struct alignas(16) DDGI
	{
		uint3 grid_dimensions;
		uint probe_count;

		uint2 color_texture_resolution;
		float2 color_texture_resolution_rcp;

		uint2 depth_texture_resolution;
		float2 depth_texture_resolution_rcp;

		float3 grid_min;
		int color_texture;

		float3 grid_extents;
		int depth_texture;

		float3 cell_size;
		float max_distance;

		float3 grid_extents_rcp;
		int offset_texture;

		float3 cell_size_rcp;
		float smooth_backface;
	};
	DDGI ddgi;
};

// Warning: the size of this structure directly affects shader performance.
//	Try to reduce it as much as possible!
//	Keep it aligned to 16 bytes for best performance!
struct alignas(16) LightEntity
{
	float3 position;
	uint type8_flags8_range16;

	uint2 direction16_coneAngleCos16; // coneAngleCos is used for cascade count in directional light
	uint2 color; // half4 packed

	uint layerMask;
	uint indices;
	uint remap;
	uint radius16_length16;

	float4 shadowAtlasMulAdd;

#ifndef __cplusplus
	// Shader-side:
	inline uint GetType()
	{
		return type8_flags8_range16 & 0xFF;
	}
	inline uint GetFlags()
	{
		return (type8_flags8_range16 >> 8u) & 0xFF;
	}
	inline half GetRange()
	{
		return (half)f16tof32(type8_flags8_range16 >> 16u);
	}
	inline half GetRadius()
	{
		return (half)f16tof32(radius16_length16);
	}
	inline half GetLength()
	{
		return (half)f16tof32(radius16_length16 >> 16u);
	}
	inline half3 GetDirection()
	{
		return normalize(half3(
			(half)f16tof32(direction16_coneAngleCos16.x),
			(half)f16tof32(direction16_coneAngleCos16.x >> 16u),
			(half)f16tof32(direction16_coneAngleCos16.y)
		));
	}
	inline half GetConeAngleCos()
	{
		return (half)f16tof32(direction16_coneAngleCos16.y >> 16u);
	}
	inline uint GetShadowCascadeCount()
	{
		return direction16_coneAngleCos16.y >> 16u;
	}
	inline half GetAngleScale()
	{
		return (half)f16tof32(remap);
	}
	inline half GetAngleOffset()
	{
		return (half)f16tof32(remap >> 16u);
	}
	inline half GetCubemapDepthRemapNear()
	{
		return (half)f16tof32(remap);
	}
	inline half GetCubemapDepthRemapFar()
	{
		return (half)f16tof32(remap >> 16u);
	}
	inline half4 GetColor()
	{
		half4 retVal;
		retVal.x = (half)f16tof32(color.x);
		retVal.y = (half)f16tof32(color.x >> 16u);
		retVal.z = (half)f16tof32(color.y);
		retVal.w = (half)f16tof32(color.y >> 16u);
		return retVal;
	}
	inline uint GetMatrixIndex()
	{
		return indices & 0xFFFF;
	}
	inline uint GetTextureIndex()
	{
		return indices >> 16u;
	}
	inline bool IsCastingShadow()
	{
		return indices != ~0;
	}
	inline half GetGravity()
	{
		return GetConeAngleCos();
	}
	inline float3 GetColliderTip()
	{
		return shadowAtlasMulAdd.xyz;
	}

#else
	// Application-side:
	inline void SetType(uint type)
	{
		type8_flags8_range16 |= type & 0xFF;
	}
	inline void SetFlags(uint flags)
	{
		type8_flags8_range16 |= (flags & 0xFF) << 8u;
	}
	inline void SetRange(float value)
	{
		type8_flags8_range16 |= XMConvertFloatToHalf(value) << 16u;
	}
	inline void SetRadius(float value)
	{
		radius16_length16 |= XMConvertFloatToHalf(value);
	}
	inline void SetLength(float value)
	{
		radius16_length16 |= XMConvertFloatToHalf(value) << 16u;
	}
	inline void SetColor(float4 value)
	{
		color.x |= XMConvertFloatToHalf(value.x);
		color.x |= XMConvertFloatToHalf(value.y) << 16u;
		color.y |= XMConvertFloatToHalf(value.z);
		color.y |= XMConvertFloatToHalf(value.w) << 16u;
	}
	inline void SetDirection(float3 value)
	{
		direction16_coneAngleCos16.x |= XMConvertFloatToHalf(value.x);
		direction16_coneAngleCos16.x |= XMConvertFloatToHalf(value.y) << 16u;
		direction16_coneAngleCos16.y |= XMConvertFloatToHalf(value.z);
	}
	inline void SetConeAngleCos(float value)
	{
		direction16_coneAngleCos16.y |= XMConvertFloatToHalf(value) << 16u;
	}
	inline void SetShadowCascadeCount(uint value)
	{
		direction16_coneAngleCos16.y |= (value & 0xFFFF) << 16u;
	}
	inline void SetAngleScale(float value)
	{
		remap |= XMConvertFloatToHalf(value);
	}
	inline void SetAngleOffset(float value)
	{
		remap |= XMConvertFloatToHalf(value) << 16u;
	}
	inline void SetCubeRemapNear(float value)
	{
		remap |= XMConvertFloatToHalf(value);
	}
	inline void SetCubeRemapFar(float value)
	{
		remap |= XMConvertFloatToHalf(value) << 16u;
	}
	inline void SetIndices(uint matrixIndex, uint textureIndex)
	{
		indices = matrixIndex & 0xFFFF;
		indices |= (textureIndex & 0xFFFF) << 16u;
	}
	inline void SetGravity(float value)
	{
		SetConeAngleCos(value);
	}
	inline void SetColliderTip(float3 value)
	{
		shadowAtlasMulAdd = float4(value.x, value.y, value.z, 0);
	}

#endif // __cplusplus
};

static const uint LIGHT_ENTITY_COUNT = 256;
static const uint LIGHT_ENTITY_TILE_BUCKET_COUNT = LIGHT_ENTITY_COUNT / 32;

struct LightEntityIterator
{
	uint value;

#ifdef __cplusplus
	LightEntityIterator(uint offset, uint count)
	{
		value = offset | (count << 16u);
	}
	constexpr operator uint() const { return value; }
#endif // __cplusplus

	inline bool empty()
	{
		return value == 0;
	}
	inline uint item_offset()
	{
		return value & 0xFFFF;
	}
	inline uint item_count()
	{
		return value >> 16u;
	}
	inline uint first_item()
	{
		return item_offset();
	}
	inline uint last_item()
	{
		return empty() ? 0 : (item_offset() + item_count() - 1);
	}
	inline uint first_bucket()
	{
		return first_item() / 32u;
	}
	inline uint last_bucket()
	{
		return last_item() / 32u;
	}
	inline uint bucket_mask()
	{
		const uint bucket_mask_lo = ~0u << first_bucket();
		const uint bucket_mask_hi = ~0u >> (31u - last_bucket());
		return bucket_mask_lo & bucket_mask_hi;
	}
	inline uint first_bucket_entity_mask()
	{
		return ~0u << (first_item() % 32u);
	}
	inline uint last_bucket_entity_mask()
	{
		return ~0u >> (31u - (last_item() % 32u));
	}
	// This masks out inactive buckets of the current type based on a whole tile bucket mask
	inline uint mask_type(uint tile_mask)
	{
		return tile_mask & bucket_mask();
	}
	// This masks out inactive entities for the current bucket type when processing either the first or the last bucket in the list
	inline uint mask_entity(uint bucket, uint bucket_bits)
	{
		if (bucket == first_bucket())
			bucket_bits &= first_bucket_entity_mask();
		if (bucket == last_bucket())
			bucket_bits &= last_bucket_entity_mask();
		return bucket_bits;
	}
};

struct alignas(16) FrameCB
{
	uint		options;					// renderer bool options packed into bitmask (OPTION_BIT_ values)
	float		time;
	float		time_previous;
	float		delta_time;

	uint		frame_count;
	uint		temporalaa_samplerotation;
	uint		padding0;
	uint		padding1;
	
	float		blue_noise_phase;
	int			texture_random64x64_index;
	int			texture_bluenoise_index;
	int			texture_sheenlut_index;

	ShaderScene scene;

	// Lights
	uint probes;	// NOTE YET SUPPORTED
	uint directional_lights;
	uint spot_lights; // NOTE YET SUPPORTED
	uint point_lights; // NOTE YET SUPPORTED

	uint lights;
	uint decals; // NOTE YET SUPPORTED
	uint padding2; // NOTE YET SUPPORTED
	uint padding3; 

	// Note: 
	//	A single renderable refers to a number of 'LightEntity's
	//	This is why LightEntities and their transform matrices do not belong to 'ShaderScene'
	LightEntity lightArray[LIGHT_ENTITY_COUNT];
	float4x4 lightMatrixArray[LIGHT_ENTITY_COUNT];
};


// ---------- Common Constant buffers: -----------------

struct alignas(16) ShaderFrustum
{
	// Frustum planes:
	//	0 : near
	//	1 : far
	//	2 : left
	//	3 : right
	//	4 : top
	//	5 : bottom
	float4 planes[6];

#ifndef __cplusplus
	inline bool Intersects(ShaderSphere sphere)
	{
		uint infrustum = 1;
		infrustum &= dot(planes[0], float4(sphere.center, 1)) > -sphere.radius;
		infrustum &= dot(planes[1], float4(sphere.center, 1)) > -sphere.radius;
		infrustum &= dot(planes[2], float4(sphere.center, 1)) > -sphere.radius;
		infrustum &= dot(planes[3], float4(sphere.center, 1)) > -sphere.radius;
		infrustum &= dot(planes[4], float4(sphere.center, 1)) > -sphere.radius;
		infrustum &= dot(planes[5], float4(sphere.center, 1)) > -sphere.radius;
		return infrustum != 0;
	}
#endif // __cplusplus
};

enum SHADERCAMERA_OPTIONS
{
	SHADERCAMERA_OPTION_NONE = 0,
	SHADERCAMERA_OPTION_USE_SHADOW_MASK = 1 << 0,
	SHADERCAMERA_OPTION_ORTHO = 1 << 1,
};
struct alignas(16) ShaderCamera
{
	float4x4	view_projection;

	float3		position;
	uint		output_index; // viewport or rendertarget array index

	float4		clip_plane;
	float4		reflection_plane; // not clip plane (not reversed when camera is under), but the original plane

	float3		forward;
	float		z_near;

	float3		up;
	float		z_far;

	float		z_near_rcp;
	float		z_far_rcp;
	float		z_range;
	float		z_range_rcp;

	float4x4	view;
	float4x4	projection;
	float4x4	inverse_view;
	float4x4	inverse_projection;
	float4x4	inverse_view_projection;

	ShaderFrustum frustum;

	float2		temporalaa_jitter;
	float2		temporalaa_jitter_prev;

	float4x4	previous_view;
	float4x4	previous_projection;
	float4x4	previous_view_projection;
	float4x4	previous_inverse_view_projection;
	float4x4	reflection_view_projection;
	float4x4	reflection_inverse_view_projection;
	float4x4	reprojection; // view_projection_inverse_matrix * previous_view_projection_matrix

	float2		aperture_shape;
	float		aperture_size;
	float		focal_length;

	uint2 internal_resolution;
	float2 internal_resolution_rcp;

	uint4 scissor; // scissor in physical coordinates (left,top,right,bottom) range: [0, internal_resolution]
	float4 scissor_uv; // scissor in screen UV coordinates (left,top,right,bottom) range: [0, 1]

	uint2 entity_culling_tilecount;
	uint entity_culling_tile_bucket_count_flat; // tilecount.x * tilecount.y * SHADER_ENTITY_TILE_BUCKET_COUNT (the total number of uint buckets for the whole screen)
	uint sample_count;

	uint2 visibility_tilecount;
	uint visibility_tilecount_flat;
	uint options;	// SHADERCAMERA_OPTIONS

	int texture_rtdiffuse_index;
	int texture_primitiveID_index;
	int texture_depth_index;
	int texture_lineardepth_index;

	int texture_velocity_index;
	int texture_normal_index;
	int texture_roughness_index;
	int buffer_entitytiles_index;

	int texture_reflection_index;
	int texture_reflection_depth_index;
	int texture_refraction_index;
	int texture_waterriples_index;

	int texture_ao_index;
	int texture_ssr_index;
	int texture_ssgi_index;
	int texture_rtshadow_index;

	int texture_surfelgi_index;
	int texture_depth_index_prev;
	int texture_vxgi_diffuse_index;
	int texture_vxgi_specular_index;

	int texture_reprojected_depth_index;
	uint padding0;
	uint padding1;
	uint padding2;

#ifdef __cplusplus
	void Init()
	{
		view_projection = {};
		position = {};
		output_index = 0;
		clip_plane = {};
		forward = {};
		z_near = {};
		up = {};
		z_far = {};
		z_near_rcp = {};
		z_far_rcp = {};
		z_range = {};
		z_range_rcp = {};
		view = {};
		projection = {};
		inverse_view = {};
		inverse_projection = {};
		inverse_view_projection = {};
		frustum = {};
		temporalaa_jitter = {};
		temporalaa_jitter_prev = {};
		previous_view = {};
		previous_projection = {};
		previous_view_projection = {};
		previous_inverse_view_projection = {};
		reflection_view_projection = {};
		reprojection = {};
		aperture_shape = {};
		aperture_size = {};
		focal_length = {};
		internal_resolution = {};
		internal_resolution_rcp = {};
		scissor = {};
		scissor_uv = {};
		entity_culling_tilecount = {};
		entity_culling_tile_bucket_count_flat = 0;
		sample_count = {};
		visibility_tilecount = {};
		visibility_tilecount_flat = {};

		texture_rtdiffuse_index = -1;
		texture_primitiveID_index = -1;
		texture_depth_index = -1;
		texture_lineardepth_index = -1;
		texture_velocity_index = -1;
		texture_normal_index = -1;
		texture_roughness_index = -1;
		buffer_entitytiles_index = -1;
		texture_reflection_index = -1;
		texture_refraction_index = -1;
		texture_waterriples_index = -1;
		texture_ao_index = -1;
		texture_ssr_index = -1;
		texture_ssgi_index = -1;
		texture_rtshadow_index = -1;
		texture_surfelgi_index = -1;
		texture_depth_index_prev = -1;
		texture_vxgi_diffuse_index = -1;
		texture_vxgi_specular_index = -1;
		texture_reprojected_depth_index = -1;

		options = 0;
	}

#else
	inline float2 clamp_uv_to_scissor(in float2 uv)
	{
		return float2(
			clamp(uv.x, scissor_uv.x, scissor_uv.z),
			clamp(uv.y, scissor_uv.y, scissor_uv.w)
		);
	}
	inline float2 clamp_pixel_to_scissor(in uint2 pixel)
	{
		return uint2(
			clamp(pixel.x, scissor.x, scissor.z),
			clamp(pixel.y, scissor.y, scissor.w)
		);
	}
	inline bool is_uv_inside_scissor(float2 uv)
	{
		return uv.x >= scissor_uv.x && uv.x <= scissor_uv.z && uv.y >= scissor_uv.y && uv.y <= scissor_uv.w;
	}
	inline bool is_pixel_inside_scissor(uint2 pixel)
	{
		return pixel.x >= scissor.x && pixel.x <= scissor.z && pixel.y >= scissor.y && pixel.y <= scissor.w;
	}
#endif // __cplusplus
};

struct alignas(16) CameraCB
{
	ShaderCamera cameras[16];

#ifdef __cplusplus
	void Init()
	{
		for (int i = 0; i < 16; ++i)
		{
			cameras[i].Init();
		}
	}
#endif // __cplusplus
};

// For binning shading based on shader types:
struct alignas(16) ShaderTypeBin
{
	uint dispatchX;
	uint dispatchY;
	uint dispatchZ;
	uint shaderType;
};

struct alignas(16) ViewTile
{
	uint64_t execution_mask;
	uint view_tile_id;
	uint entity_flat_tile_index;

	inline bool check_thread_valid(uint groupIndex)
	{
		return (execution_mask & (uint64_t(1) << uint64_t(groupIndex))) != 0;
	}
};

#endif