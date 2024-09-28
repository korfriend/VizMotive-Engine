#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
	void SetCanvas(const uint32_t w, const uint32_t h, const float dpi, void* window)
	{

	}
	void GetCanvas(uint32_t* w, uint32_t* h, float* dpi, void** window = nullptr);

	void SetViewport(const uint32_t x, const uint32_t y, const uint32_t w, const uint32_t h);
	void GetViewport(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h);

	void SetVisibleLayerMask(const uint8_t layerBits, const uint8_t maskBits);

	VZRESULT Render(const VID vidScene, const VID vidCam);
}