#include "RenderPath3D_Detail.h"

namespace vz::renderer
{
	void RenderableShapeCollection::drawAndClearLines(const CameraComponent& camera, std::vector<RenderableLine>& renderableLines, CommandList cmd)
	{
		static GraphicsDevice* device = graphics::GetDevice();

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

		renderableLines.clear();
	};
}