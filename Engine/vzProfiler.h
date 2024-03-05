#pragma once
#include "vzGraphicsDevice.h"
#include "vzCanvas.h"
#include "vzColor.h"

namespace vz::profiler
{
	typedef size_t range_id;

	// Begin collecting profiling data for the current frame
	void BeginFrame();

	// Finalize collecting profiling data for the current frame
	void EndFrame(vz::graphics::CommandList cmd);

	// Start a CPU profiling range
	range_id BeginRangeCPU(const char* name);

	// Start a GPU profiling range
	range_id BeginRangeGPU(const char* name, vz::graphics::CommandList cmd);

	// End a profiling range
	void EndRange(range_id id);

	// Renders a basic text of the Profiling results to the (x,y) screen coordinate
	void DrawData(
		const vz::Canvas& canvas,
		float x,
		float y,
		vz::graphics::CommandList cmd,
		vz::graphics::ColorSpace colorspace = vz::graphics::ColorSpace::SRGB
	);
	void DisableDrawForThisFrame();

	// Enable/disable profiling
	void SetEnabled(bool value);

	bool IsEnabled();

	void SetBackgroundColor(vz::Color color);
	void SetTextColor(vz::Color color);
};

