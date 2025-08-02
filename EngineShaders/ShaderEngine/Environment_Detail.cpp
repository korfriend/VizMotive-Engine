#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	void GRenderPath3DDetails::DrawSun(CommandList cmd)
	{
		device->EventBegin("DrawSun", cmd);

		device->BindPipelineState(&PSO_sky[SKY_RENDERING_SUN], cmd);

		BindCommonResources(cmd);

		device->Draw(3, 0, cmd);

		device->EventEnd(cmd);
	}

	void GRenderPath3DDetails::DrawSky(CommandList cmd)
	{
		device->EventBegin("DrawSky", cmd);
		
		if (scene_Gdetails->envrironment->skyMap.IsValid())
		{
			device->BindPipelineState(&PSO_sky[SKY_RENDERING_STATIC], cmd);
		}
		else
		{
			device->BindPipelineState(&PSO_sky[SKY_RENDERING_DYNAMIC], cmd);
		}

		BindCommonResources(cmd);

		device->Draw(3, 0, cmd);

		device->EventEnd(cmd);
	}

}
