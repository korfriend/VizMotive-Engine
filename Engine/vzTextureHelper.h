#pragma once
#include "CommonInclude.h"
#include "vzGraphicsDevice.h"
#include "vzColor.h"

namespace vz::texturehelper
{
	void Initialize();

	const vz::graphics::Texture* getLogo();
	const vz::graphics::Texture* getRandom64x64();
	const vz::graphics::Texture* getColorGradeDefault();
	const vz::graphics::Texture* getNormalMapDefault();
	const vz::graphics::Texture* getBlackCubeMap();
	const vz::graphics::Texture* getUINT4();
	const vz::graphics::Texture* getBlueNoise();
	const vz::graphics::Texture* getWaterRipple();

	const vz::graphics::Texture* getWhite();
	const vz::graphics::Texture* getBlack();
	const vz::graphics::Texture* getTransparent();

	bool CreateTexture(
		vz::graphics::Texture& texture,
		const uint8_t* data,
		uint32_t width,
		uint32_t height,
		vz::graphics::Format format = vz::graphics::Format::R8G8B8A8_UNORM,
		vz::graphics::Swizzle swizzle = {}
	);

	enum class GradientType
	{
		Linear,
		Circular,
		Angular,
	};
	enum class GradientFlags
	{
		None = 0,
		Inverse = 1 << 0,		// inverts resulting gradient
		Smoothstep = 1 << 1,	// applies smoothstep function to resulting gradient
		PerlinNoise = 1 << 2,	// applies perlin noise to gradient
		R16Unorm = 1 << 3,		// the texture will be created in R16_UNORM format instead of R8_UNORM
	};
	vz::graphics::Texture CreateGradientTexture(
		GradientType type,
		uint32_t width,
		uint32_t height,
		const XMFLOAT2& uv_start = XMFLOAT2(0, 0),
		const XMFLOAT2& uv_end = XMFLOAT2(1, 0),
		GradientFlags gradient_flags = GradientFlags::None,
		vz::graphics::Swizzle swizzle = { vz::graphics::ComponentSwizzle::R, vz::graphics::ComponentSwizzle::R, vz::graphics::ComponentSwizzle::R, vz::graphics::ComponentSwizzle::R },
		float perlin_scale = 1,
		uint32_t perlin_seed = 1234u,
		int perlin_octaves = 8,
		float perlin_persistence = 0.5f
	);

	// Similar to CreateGradientTexture() with GradientType::Angular type but different parameters
	vz::graphics::Texture CreateCircularProgressGradientTexture(
		uint32_t width,
		uint32_t height,
		const XMFLOAT2& direction = XMFLOAT2(0, 1),
		bool counter_clockwise = false,
		vz::graphics::Swizzle swizzle = { vz::graphics::ComponentSwizzle::R, vz::graphics::ComponentSwizzle::R, vz::graphics::ComponentSwizzle::R, vz::graphics::ComponentSwizzle::R }
	);
};

template<>
struct enable_bitmask_operators<vz::texturehelper::GradientFlags> {
	static const bool enable = true;
};
