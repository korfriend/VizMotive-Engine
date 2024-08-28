#pragma once
#include "CommonInclude.h"
#include "Libs/Math.h"

namespace vz
{
	// The canvas specifies a DPI-aware drawing area
	struct Canvas
	{
	private:
		uint32_t width_ = 0;
		uint32_t height_ = 0;
		float dpi_ = 96;
		float scaling_ = 1; // custom DPI scaling factor (optional)
		void* window_ = nullptr;

	public:
		virtual ~Canvas() = default;

		// Create a canvas from physical measurements
		inline void SetCanvas(uint32_t width, uint32_t height, float dpi = 96, void* window = nullptr)
		{
			width_ = width;
			height_ = height;
			dpi_ = dpi;
			window_ = window;
		}

		// How many pixels there are per inch
		inline float GetDPI() const { return dpi_; }
		// The scaling factor between logical and physical coordinates
		inline float GetDPIScaling() const { return GetDPI() / 96.f; }
		// Convert from logical to physical coordinates
		inline uint32_t LogicalToPhysical(float logical) const { return uint32_t(logical * GetDPIScaling()); }
		// Convert from physical to logical coordinates
		inline float PhysicalToLogical(uint32_t physical) const { return float(physical) / GetDPIScaling(); }
		// Returns native resolution width in pixels:
		//	Use this for texture allocations
		//	Use this for scissor, viewport
		inline uint32_t GetPhysicalWidth() const { return width_; }
		// Returns native resolution height in pixels:
		//	Use this for texture allocations
		//	Use this for scissor, viewport
		inline uint32_t GetPhysicalHeight() const { return height_; }
		// Returns the width with DPI scaling applied (subpixel size):
		//	Use this for logic and positioning drawable elements
		inline float GetLogicalWidth() const { return PhysicalToLogical(GetPhysicalWidth()); }
		// Returns the height with DPI scaling applied (subpixel size):
		//	Use this for logic and positioning drawable elements
		inline float GetLogicalHeight() const { return PhysicalToLogical(GetPhysicalHeight()); }

	};
}
