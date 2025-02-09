#pragma once
#include "Libs/vzMath.h"
#include "GBackend/GBackendDevice.h"


#include <string>

namespace vz
{
	using Entity = uint32_t;
	struct Canvas;
	class RenderPath3D;

	namespace canvas
	{
		Canvas* GetCanvas(const Entity entity);
		Canvas* GetFirstCanvasByName(const std::string& name);
		RenderPath3D* CreateRenderPath3D(graphics::GraphicsDevice* graphicsDevice, const std::string& name, const Entity entity = 0);
		bool DestroyCanvas(const Entity entity);
		void DestroyAll();
	}

	// The canvas specifies a DPI-aware drawing area
	struct Canvas
	{
	protected:
		uint32_t width_ = 0;	// minimum 16
		uint32_t height_ = 0;	// minimum 16
		float dpi_ = 96;
		float scaling_ = 1; // custom DPI scaling factor (optional)
		void* window_ = nullptr;
		Entity entity_ = 0;
		std::string type_ = "CANVAS";

	public:
		Canvas(const Entity entity) : entity_(entity) {};
		virtual ~Canvas() = default;

		bool allowHDR = true;
		std::string name = "";

		std::string GetType() const { return type_; }

		// Create a canvas from physical measurements
		inline void SetCanvas(uint32_t width, uint32_t height, float dpi = 96, void* window = nullptr)
		{
			width_ = std::max(width, 32u);
			height_ = std::max(height, 32u);
			dpi_ = dpi;
			window_ = window;
		}
		inline Entity GetEntity() const { return entity_; }
		// How many pixels there are per inch
		constexpr float GetDPI() const { return dpi_; }
		inline void* GetWindow() const { return window_; }
		// The scaling factor between logical and physical coordinates
		constexpr float GetDPIScaling() const { return GetDPI() / 96.f; }
		// Convert from logical to physical coordinates
		constexpr uint32_t LogicalToPhysical(float logical) const { return uint32_t(logical * GetDPIScaling()); }
		// Convert from physical to logical coordinates
		constexpr float PhysicalToLogical(uint32_t physical) const { return float(physical) / GetDPIScaling(); }
		// Returns native resolution width in pixels:
		//	Use this for texture allocations
		//	Use this for scissor, viewport
		constexpr uint32_t GetPhysicalWidth() const { return width_; }
		// Returns native resolution height in pixels:
		//	Use this for texture allocations
		//	Use this for scissor, viewport
		constexpr uint32_t GetPhysicalHeight() const { return height_; }
		// Returns the width with DPI scaling applied (subpixel size):
		//	Use this for logic and positioning drawable elements
		constexpr float GetLogicalWidth() const { return PhysicalToLogical(GetPhysicalWidth()); }
		// Returns the height with DPI scaling applied (subpixel size):
		//	Use this for logic and positioning drawable elements
		constexpr float GetLogicalHeight() const { return PhysicalToLogical(GetPhysicalHeight()); }
		// Returns projection matrix that maps logical to physical space
		//	Use this to render to a graphics viewport
		inline XMMATRIX GetProjection() const
		{
			return VZMatrixOrthographicOffCenter(0, (float)GetLogicalWidth(), (float)GetLogicalHeight(), 0, -1, 1);
		}
		// Returns the aspect (width/height)
		constexpr float GetAspect() const
		{
			if (height_ == 0)
				return float(width_);
			return float(width_) / float(height_);
		}
	};
}
