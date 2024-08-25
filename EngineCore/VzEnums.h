#pragma once

namespace vz::enums
{
	enum class SceneNodeClassType // every component involves a transform and a name
	{
		SCENEBASE = 0,  // empty (only transform and name)
		CAMERA,
		LIGHT,
		ACTOR,
		SPRITE_ACTOR,
		TEXT_SPRITE_ACTOR,
	};

	enum class ResClassType
	{
		RESOURCE = 0,
		GEOMATRY,
		MATERIAL,
		MATERIALINSTANCE,
		TEXTURE,
		FONT,
	};

	enum class EulerAngle { XYZ, YXZ, ZXY, ZYX, YZX, XZY };

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
}
