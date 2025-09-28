#include "DebugRenderer.h"
#include "Font.h"
#include "Utils/Color.h"

namespace vz::renderer
{
	void DebugShapeCollection::drawAndClearLines(const CameraComponent& camera, std::vector<RenderableLine>& renderableLines, CommandList cmd, bool clearEnabled)
	{
		if (renderableLines.empty())
			return;
		
		GraphicsDevice* device = GetDevice();

		MiscCB sb;
		sb.g_xTransform = camera.GetViewProjection();
		sb.g_xColor = XMFLOAT4(1, 1, 1, 1);
		sb.g_xThickness = depthLineThicknessPixel;
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

	void DebugShapeCollection::DrawLines(const CameraComponent& camera, CommandList cmd, bool clearEnabled)
	{
		GraphicsDevice* device = GetDevice();

		device->EventBegin("DEBUG DRAW: Lines - 3D", cmd);

		device->BindPipelineState(&PSO_RenderableShapes[DEBUG_RENDERING_LINES], cmd);
		drawAndClearLines(camera, renderableLines_, cmd, clearEnabled);

		device->BindPipelineState(&PSO_RenderableShapes[DEBUG_RENDERING_LINES_DEPTH], cmd);
		drawAndClearLines(camera, renderableLines_depth_, cmd, clearEnabled);

		// GPU-generated indirect debug lines:
		device->BindPipelineState(&PSO_RenderableShapes[DEBUG_RENDERING_LINES], cmd);
		{
			device->EventBegin("Indirect Debug Lines - 3D", cmd);

			MiscCB sb;
			sb.g_xTransform = camera.GetViewProjection();
			sb.g_xColor = XMFLOAT4(1, 1, 1, 1);
			device->BindDynamicConstantBuffer(sb, CB_GETBINDSLOT(MiscCB), cmd);

			const GPUBuffer* vbs[] = {
				&buffers[BUFFERTYPE_INDIRECT_DEBUG_1],
			};
			const uint32_t strides[] = {
				sizeof(XMFLOAT4) + sizeof(XMFLOAT4),
			};
			const uint64_t offsets[] = {
				sizeof(IndirectDrawArgsInstanced),
			};
			device->BindVertexBuffers(vbs, 0, arraysize(vbs), strides, offsets, cmd);

			device->DrawInstancedIndirect(&buffers[BUFFERTYPE_INDIRECT_DEBUG_1], 0, cmd);

			device->EventEnd(cmd);
		}
		device->EventEnd(cmd);
	}

	void DebugShapeCollection::AddPrimitivePart(const GeometryComponent::Primitive& part, const XMFLOAT4& baseColor, const XMFLOAT4X4& world)
	{
		XMVECTOR xbaseColor = XMLoadFloat4(&baseColor);
		XMMATRIX W = XMLoadFloat4x4(&world);
		auto colorLine = [&xbaseColor](const Color& color) {
			XMFLOAT4 color4 = color.toFloat4();
			XMStoreFloat4(&color4, XMLoadFloat4(&color4) * xbaseColor);
			return color4;
			};

		const std::vector<XMFLOAT3>& positions = part.GetVtxPositions();
		const std::vector<uint>& colors = part.GetVtxColors();
		bool is_color_vtx = positions.size() == colors.size();
		const std::vector<uint32_t>& indices = part.GetIdxPrimives();

		switch (part.GetPrimitiveType())
		{
		case GeometryComponent::PrimitiveType::LINE_STRIP:
		{
			size_t n = part.GetNumVertices() - 1;
			for (size_t line_idx = 0; line_idx < n; ++line_idx)
			{
				RenderableLine line;
				XMStoreFloat3(&line.start, XMVector3TransformCoord(XMLoadFloat3(&positions[line_idx]), W));
				XMStoreFloat3(&line.end, XMVector3TransformCoord(XMLoadFloat3(&positions[line_idx + 1]), W));
				if (is_color_vtx)
				{
					line.color_start = colorLine(Color(colors[line_idx]));
					line.color_end = colorLine(Color(colors[line_idx + 1]));
				}
				else
				{
					line.color_start = line.color_end = baseColor;
				}
				AddDrawLine(line, true);
			}
		} break;
		case GeometryComponent::PrimitiveType::LINES:
		{
			size_t n = part.GetNumIndices() / 2;
			for (size_t line_idx = 0; line_idx < n; ++line_idx)
			{
				RenderableLine line;
				uint32_t idx0 = indices[2 * line_idx + 0];
				uint32_t idx1 = indices[2 * line_idx + 1];
				XMStoreFloat3(&line.start, XMVector3TransformCoord(XMLoadFloat3(&positions[idx0]), W));
				XMStoreFloat3(&line.end, XMVector3TransformCoord(XMLoadFloat3(&positions[idx1]), W));
				if (is_color_vtx)
				{
					line.color_start = colorLine(Color(colors[idx0]));
					line.color_end = colorLine(Color(colors[idx1]));
				}
				else
				{
					line.color_start = line.color_end = baseColor;
				}
				AddDrawLine(line, true);
			}
		} break;
		case GeometryComponent::PrimitiveType::POINTS:
		default:
			break;
		}
	}

	void DebugShapeCollection::DrawDebugTextStorage(const CameraComponent& camera, CommandList cmd, bool clearEnabled)
	{
		GraphicsDevice* device = GetDevice();

		device->EventBegin("DebugTexts", cmd);
		const XMMATRIX VP = XMLoadFloat4x4(&camera.GetViewProjection());
		const XMMATRIX R = XMLoadFloat3x3(&camera.GetRotationToFaceCamera());
		struct DebugTextSorter
		{
			DebugTextParams params;
			float distance;
		};
		static thread_local std::vector<DebugTextSorter> sorted_texts;
		sorted_texts.clear();
		size_t offset = 0;

		for (size_t i = 0; i < renderableTextStorages_.size(); ++i)
		{
			auto& x = sorted_texts.emplace_back();
			x.params = renderableTextStorages_[i];
			x.distance = math::Distance(x.params.position, camera.GetWorldEye());
		}
		std::sort(sorted_texts.begin(), sorted_texts.end(), [](const DebugTextSorter& a, const DebugTextSorter& b) {
			return a.distance > b.distance;
			});
		for (auto& x : sorted_texts)
		{
			font::Params params;
			params.position = x.params.position;
			params.size = x.params.pixel_height;
			params.scaling = 1.0f / params.size * x.params.scaling;
			params.color = Color::fromFloat4(x.params.color);
			params.intensity = x.params.color.w > 1 ? x.params.color.w : 1;
			params.h_align = font::Alignment::FONTALIGN_CENTER;
			params.v_align = font::Alignment::FONTALIGN_CENTER;
			params.softness = 0.0f;
			params.shadowColor = Color::Black();
			params.shadow_softness = 0.8f;
			params.customProjection = &VP;
			if (x.params.flags & DebugTextParams::DEPTH_TEST)
			{
				params.enableDepthTest();
			}
			if (x.params.flags & DebugTextParams::CAMERA_FACING)
			{
				params.customRotation = &R;
			}
			if (x.params.flags & DebugTextParams::CAMERA_SCALING)
			{
				params.scaling *= x.distance * 0.05f;
			}
			font::Draw(x.params.text, params, cmd);
		}
		if (clearEnabled)
		{
			renderableTextStorages_.clear();
		}
		device->EventEnd(cmd);
	}
}