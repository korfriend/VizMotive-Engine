#pragma once

// common numerations used for both high-level APIs and engine-level APIs
namespace vz::enums
{
	enum class ResClassType
	{
		RESOURCE = 0,
		GEOMATRY,
		MATERIAL,
		MATERIALINSTANCE,
		TEXTURE,
		FONT,
	};

	enum class EulerAngle { ZXY, ZYX, XYZ, YXZ, YZX, XZY };

	enum class PrimitiveType : uint8_t {
		// don't change the enums values (made to match GL)
		POINTS = 0,    //!< points
		LINES = 1,    //!< lines
		LINE_STRIP = 3,    //!< line strip
		TRIANGLES = 4,    //!< triangles
		TRIANGLE_STRIP = 5     //!< triangle strip
	};

	enum LightFlags
	{
		EMPTY = 0,
		CAST_SHADOW = 1 << 0,
		VOLUMETRICS = 1 << 1,
		VISUALIZER = 1 << 2,
		LIGHTMAPONLY_STATIC = 1 << 3,
		VOLUMETRICCLOUDS = 1 << 4,
	};

	enum LightType
	{
		DIRECTIONAL = 0,
		POINT,
		SPOT,
		LIGHTTYPE_COUNT,
		ENUM_FORCE_UINT32 = 0xFFFFFFFF,
	};

	enum class UniformType : uint8_t {
		BOOL,
		BOOL2,
		BOOL3,
		BOOL4,
		FLOAT,
		FLOAT2,
		FLOAT3,
		FLOAT4,
		INT,
		INT2,
		INT3,
		INT4,
		UINT,
		UINT2,
		UINT3,
		UINT4,
		MAT3,   //!< a 3x3 float matrix
		MAT4,   //!< a 4x4 float matrix
		STRUCT
	};

	enum class Precision : uint8_t {
		LOW,
		MEDIUM,
		HIGH,
		DEFAULT
	};

	enum class SamplerType : uint8_t {
		SAMPLER_2D,             //!< 2D texture
		SAMPLER_2D_ARRAY,       //!< 2D array texture
		SAMPLER_CUBEMAP,        //!< Cube map texture
		SAMPLER_EXTERNAL,       //!< External texture
		SAMPLER_3D,             //!< 3D texture
		SAMPLER_CUBEMAP_ARRAY,  //!< Cube map array texture (feature level 2)
	};

	enum class SubpassType : uint8_t {
		SUBPASS_INPUT
	};

	enum class SamplerFormat : uint8_t {
		INT = 0,        //!< signed integer sampler
		UINT = 1,       //!< unsigned integer sampler
		FLOAT = 2,      //!< float sampler
		SHADOW = 3      //!< shadow sampler (PCF)
	};

	enum class Interpolation : uint8_t {
		SMOOTH,                 //!< default, smooth interpolation
		FLAT                    //!< flat interpolation
	};

	enum class Shading : uint8_t {
		UNLIT,                  //!< no lighting applied, emissive possible
		LIT,                    //!< default, standard lighting
		SUBSURFACE,             //!< subsurface lighting model
		CLOTH,                  //!< cloth lighting model
	};
}
