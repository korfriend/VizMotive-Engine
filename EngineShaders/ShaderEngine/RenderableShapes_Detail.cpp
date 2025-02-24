#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	enum DEBUGRENDERING
	{
		DEBUGRENDERING_LINES,
		DEBUGRENDERING_LINES_DEPTH,
		DEBUGRENDERING_COUNT
	};

	GraphicsDevice* device = nullptr;
	PipelineState PSO_RenderableShapes[DEBUGRENDERING_COUNT];

	void RenderableShapeCollection::Initialize()
	{
		device = graphics::GetDevice();

		jobsystem::context ctx;
		jobsystem::Dispatch(ctx, DEBUGRENDERING_COUNT, 1, [](jobsystem::JobArgs args) {
			PipelineStateDesc desc;

			switch (args.jobIndex)
			{
			case DEBUGRENDERING_LINES:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHDISABLED];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			case DEBUGRENDERING_LINES_DEPTH:
				desc.vs = &shaders[VSTYPE_VERTEXCOLOR];
				desc.ps = &shaders[PSTYPE_VERTEXCOLOR];
				desc.il = &inputLayouts[ILTYPE_VERTEXCOLOR];
				desc.dss = &depthStencils[DSSTYPE_DEPTHREAD];
				desc.rs = &rasterizers[RSTYPE_WIRE_DOUBLESIDED_SMOOTH];
				desc.bs = &blendStates[BSTYPE_TRANSPARENT];
				desc.pt = PrimitiveTopology::LINELIST;
				break;
			}

			device->CreatePipelineState(&desc, &PSO_RenderableShapes[args.jobIndex]);
			});
	}
	void RenderableShapeCollection::Deinitialize()
	{
		ReleaseRenderRes(PSO_RenderableShapes, DEBUGRENDERING_COUNT);
	}
}
namespace vz::renderer
{

	void RenderableShapeCollection::drawAndClearLines(const CameraComponent& camera, std::vector<RenderableLine>& renderableLines, CommandList cmd, bool clearEnabled)
	{
		if (renderableLines.empty())
			return;
		MiscCB sb;
		sb.g_xTransform = camera.GetViewProjection();
		sb.g_xColor = XMFLOAT4(1, 1, 1, 1);
		device->BindDynamicConstantBuffer(sb, CB_GETBINDSLOT(MiscCB), cmd);

		struct LineSegment
		{
			XMFLOAT4 a, colorA, b, colorB;
		};
		GraphicsDevice::GPUAllocation mem = device->AllocateGPU(sizeof(LineSegment) * renderableLines.size(), cmd);

		int i = 0;
		for (auto& line : renderableLines)
		{
			LineSegment segment;
			segment.a = XMFLOAT4(line.start.x, line.start.y, line.start.z, 1);
			segment.b = XMFLOAT4(line.end.x, line.end.y, line.end.z, 1);
			segment.colorA = line.color_start;
			segment.colorB = line.color_end;

			memcpy((void*)((size_t)mem.data + i * sizeof(LineSegment)), &segment, sizeof(LineSegment));
			i++;
		}

		const GPUBuffer* vbs[] = {
			&mem.buffer,
		};
		const uint32_t strides[] = {
			sizeof(XMFLOAT4) + sizeof(XMFLOAT4),
		};
		const uint64_t offsets[] = {
			mem.offset,
		};
		device->BindVertexBuffers(vbs, 0, arraysize(vbs), strides, offsets, cmd);

		device->Draw(2 * i, 0, cmd);

		if (clearEnabled)
		{
			renderableLines.clear();
		}
	};

	void RenderableShapeCollection::DrawLines(const CameraComponent& camera, CommandList cmd, bool clearEnabled)
	{
		device->EventBegin("Draw Lines - 3D", cmd);
		device->BindPipelineState(&PSO_RenderableShapes[DEBUGRENDERING_LINES], cmd);
		drawAndClearLines(camera, renderableLines_, cmd, clearEnabled);

		device->BindPipelineState(&PSO_RenderableShapes[DEBUGRENDERING_LINES_DEPTH], cmd);
		drawAndClearLines(camera, renderableLines_depth_, cmd, clearEnabled);
		device->EventEnd(cmd);
	}
}