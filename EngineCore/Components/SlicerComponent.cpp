#include "GComponents.h"
#include "Common/Engine_Internal.h"
#include "GBackend/GModuleLoader.h" // deferred task for streaming
#include "Utils/Backlog.h"
#include "Utils/Color.h"

namespace vz
{
	extern GShaderEngineLoader shaderEngine;

	void SlicerComponent::UpdateCurve()
	{
		if (!isDirtyCurve_)
		{
			return;
		}
		vzlog_assert(IsCurvedSlicer(), "SlicerComponent::updateCurve() is allowed only for curved slicer!");

		size_t num_ctrs = horizontalCurveControls_.size();
		if (num_ctrs < 2)
		{
			return;
		}

		XMFLOAT3 pos_0 = horizontalCurveControls_[0];
		float minInterval = FLT_MAX;
		for (size_t i = 1; i < num_ctrs; i++) {
			XMFLOAT3 pos_1 = horizontalCurveControls_[i];
			minInterval = std::min(minInterval, XMVectorGetX(XMVector3Length(XMVector3Length(XMLoadFloat3(&pos_0) - XMLoadFloat3(&pos_1)))));
			pos_0 = pos_1;
		}
		if (curveInterpolationInterval_ >= minInterval * 0.1f) {
			vzlog_warning("curveInterpolationInterval_(%f) is undesirably large, resulting in noisy geometry, compared to minimal segment length (%f)", curveInterpolationInterval_, minInterval);
		}

		geometrics::Curve curve(horizontalCurveControls_, false, geometrics::CurveType::CENTRIPETAL);

		std::vector<XMFLOAT3> curve_interpolation;
		curve_interpolation.reserve(100000);

		float seg_ratio = 1.f / (float)(num_ctrs - 1);

		for (size_t i = 0; i < num_ctrs - 1; i++) { // numCtrPts - 1
			XMFLOAT3 pos0 = horizontalCurveControls_[i];
			XMFLOAT3 pos1 = horizontalCurveControls_[i + 1];
			float segLength = XMVectorGetX(XMVector3Length(XMVector3Length(XMLoadFloat3(&pos0) - XMLoadFloat3(&pos1))));
			int num = (int)(10 * segLength / curveInterpolationInterval_ + 0.5);
			if (num < 2)
				continue;

			for (size_t j = 0; j < num; j++) {
				float t = (float)j / (float)num;
				XMFLOAT3 posCurve = curve.getPoint(t * seg_ratio + seg_ratio * i);
				curve_interpolation.push_back(posCurve);
			}
		}

		if (curve_interpolation.empty())
			return;

		horizontalCurveInterpPoints_.clear();
		horizontalCurveInterpPoints_.push_back(curve_interpolation.front());

		// reparameterize by arc length
		float accumulated_distance = 0.0f;
		float target_distance = curveInterpolationInterval_;

		for (size_t i = 1; i < curve_interpolation.size(); i++)
		{
			XMVECTOR p0 = XMLoadFloat3(&curve_interpolation[i - 1]);
			XMVECTOR p1 = XMLoadFloat3(&curve_interpolation[i]);

			float segment_length = XMVectorGetX(XMVector3Length(p1 - p0));

			while (accumulated_distance + segment_length >= target_distance)
			{
				float t = (target_distance - accumulated_distance) / segment_length;

				XMVECTOR interp_p = XMVectorLerp(p0, p1, t);
				XMFLOAT3 new_point;
				XMStoreFloat3(&new_point, interp_p);

				horizontalCurveInterpPoints_.push_back(new_point);

				target_distance += curveInterpolationInterval_;
			}

			accumulated_distance += segment_length;
		}

		curvedPlaneWidth_ = accumulated_distance;

		// Add the endpoint if the last sampled point is not sufficiently close to the curve's end		
		{
			XMFLOAT3 last_sampled_p = horizontalCurveInterpPoints_.back();
			XMFLOAT3 curve_end_p = curve_interpolation.back();
			float end_distance = XMVectorGetX(XMVector3Length(XMLoadFloat3(&last_sampled_p) - XMLoadFloat3(&curve_end_p)));

			if (end_distance > curveInterpolationInterval_ * 0.5f) {
				horizontalCurveInterpPoints_.push_back(curve_end_p);
			}
		}

		isDirtyCurve_ = false;
		timeStampSetter_ = TimerNow;
	}

	void GSlicerComponent::UpdateCurve()
	{
		SlicerComponent::UpdateCurve();

		curveInterpPointsBuffer = {};

		using namespace graphics;
		GraphicsDevice* device = GetDevice();

		GPUBufferDesc desc;
		desc.stride = sizeof(float);
		desc.size = desc.stride * horizontalCurveInterpPoints_.size() * 3;

		desc.bind_flags = BindFlag::SHADER_RESOURCE;
		desc.misc_flags = ResourceMiscFlag::BUFFER_RAW;
		desc.usage = Usage::DEFAULT;
		device->CreateBuffer(&desc, nullptr, &curveInterpPointsBuffer);
		device->SetName(&curveInterpPointsBuffer, "GSlicerComponent::curveInterpPointsBuffer");

		if (shaderEngine.pluginAddDeferredBufferUpdate)
		{
			shaderEngine.pluginAddDeferredBufferUpdate(curveInterpPointsBuffer, horizontalCurveInterpPoints_.data(), desc.size, 0);
		}
	}

	bool SlicerComponent::MakeCurvedSlicerHelperGeometry(const Entity geometryEntity)
	{
		vzlog_assert(IsCurvedSlicer(), "SlicerComponent::MakeCurvedSlicerHelperGeometry() is allowed only for curved slicer!");

		GeometryComponent* geometry = compfactory::GetGeometryComponent(geometryEntity);
		if (geometry == nullptr)
		{
			return false;
		}

		if (isDirtyCurve_)
		{
			UpdateCurve();
		}

		if (horizontalCurveInterpPoints_.size() == 0)
		{
			vzlog_error("Slicer (%llu) has NO curve setting", entity_);
			return false;
		}

		using Primitive = GeometryComponent::Primitive;
		using PrimitiveType = GeometryComponent::PrimitiveType;
		using namespace std;

		vector<Primitive> primitives(2);
		Primitive& primitive_panoplane = primitives[0];
		Primitive& primitive_lines = primitives[1];

		vector<XMFLOAT3>& pos_curve_pts = horizontalCurveInterpPoints_;
		size_t num_pts = pos_curve_pts.size();

		XMVECTOR vec_up = XMLoadFloat3(&curvedSlicerUp_);
		
		float curve_plane_height = curvedPlaneHeight_;

		geometrics::AABB aabb;
		geometrics::AABB aabb_plane;

		// lines
		{
			// outline + center line + thickness lines
			size_t num_line_points = num_pts * 2 + num_pts;
			size_t num_indices = num_pts * 2 * 2 + (num_pts - 1) * 2;
			if (thickness_ > FLT_EPSILON)
			{
				num_line_points += num_pts * 2 * 2;
				num_indices += num_pts * 2 * 2 * 2 + 4 * 2;
			}

			primitive_lines.SetPrimitiveType(PrimitiveType::LINES);
			vector<XMFLOAT3>& pos_lines = primitive_lines.GetMutableVtxPositions();
			pos_lines.resize(num_line_points);
			vector<uint32_t>& color_lines = primitive_lines.GetMutableVtxColors();
			color_lines.resize(num_line_points);
			vector<uint32_t>& idx_lines = primitive_lines.GetMutableIdxPrimives();
			idx_lines.resize(num_indices);
			aabb = geometrics::AABB();

			uint32_t vtx_offset = 0;
			uint32_t idx_offset = 0;
			// outline
			{
				Color outline_color(0, 255, 0, 255);
				for (size_t i = 0, n = num_pts; i < n; ++i)
				{
					XMStoreFloat3(&pos_lines[vtx_offset + i], XMLoadFloat3(&pos_curve_pts[i]) + vec_up * curve_plane_height * 0.5f);
					XMStoreFloat3(&pos_lines[vtx_offset + i + num_pts], XMLoadFloat3(&pos_curve_pts[i]) - vec_up * curve_plane_height * 0.5f);
					color_lines[vtx_offset + i] = outline_color;
					color_lines[vtx_offset + i + num_pts] = outline_color;

					XMFLOAT3 p = pos_lines[vtx_offset + i];
					aabb._max.x = max(aabb._max.x, p.x);
					aabb._max.y = max(aabb._max.y, p.y);
					aabb._max.z = max(aabb._max.z, p.z);
					aabb._min.x = min(aabb._min.x, p.x);
					aabb._min.y = min(aabb._min.y, p.y);
					aabb._min.z = min(aabb._min.z, p.z);

					p = pos_lines[vtx_offset + i + num_pts];
					aabb._max.x = max(aabb._max.x, p.x);
					aabb._max.y = max(aabb._max.y, p.y);
					aabb._max.z = max(aabb._max.z, p.z);
					aabb._min.x = min(aabb._min.x, p.x);
					aabb._min.y = min(aabb._min.y, p.y);
					aabb._min.z = min(aabb._min.z, p.z);

					if (i < num_pts - 1)
					{
						idx_lines[idx_offset + 2 * i + 0] = vtx_offset + i;
						idx_lines[idx_offset + 2 * i + 1] = vtx_offset + i + 1;

						idx_lines[idx_offset + 2 * i + 0 + 2 * (num_pts - 1)] = vtx_offset + i + num_pts;
						idx_lines[idx_offset + 2 * i + 1 + 2 * (num_pts - 1)] = vtx_offset + i + 1 + num_pts;
					}
				}
				idx_lines[idx_offset + 4 * (num_pts - 1) + 0] = vtx_offset + 0;
				idx_lines[idx_offset + 4 * (num_pts - 1) + 1] = vtx_offset + num_pts;

				idx_lines[idx_offset + 4 * (num_pts - 1) + 2] = vtx_offset + num_pts - 1;
				idx_lines[idx_offset + 4 * (num_pts - 1) + 3] = vtx_offset + 2 * num_pts - 1;
			}
			aabb_plane = aabb;

			// center line
			{
				vtx_offset += num_pts * 2;
				idx_offset += num_pts * 2 * 2;
				Color centerline_color(255, 0, 0, 255);
				for (size_t i = 0, n = num_pts; i < n; ++i)
				{
					XMFLOAT3 p = pos_curve_pts[i];
					pos_lines[vtx_offset + i] = p;
					color_lines[vtx_offset + i] = centerline_color;

					if (i < num_pts - 1)
					{
						idx_lines[idx_offset + 2 * i + 0] = vtx_offset + i;
						idx_lines[idx_offset + 2 * i + 1] = vtx_offset + i + 1;
					}
				}
			}

			// thickness line
			if (thickness_ > FLT_EPSILON)
			{
				vtx_offset += num_pts;
				idx_offset += (num_pts - 1) * 2;

				Color outline_color(55, 255, 55, 255);
				XMVECTOR vec_tan;
				assert(num_pts >= 2);

				uint32_t corners[8];
				for (size_t k = 0; k < 2; ++k)
				{
					float signed_thickness = k == 0 ? thickness_ : -thickness_;

					for (size_t i = 0, n = num_pts; i < n; ++i)
					{
						if (i < num_pts - 1)
						{
							vec_tan = XMLoadFloat3(&pos_curve_pts[i + 1]) - XMLoadFloat3(&pos_curve_pts[i]);
						}
						XMVECTOR vec_dir = XMVector3Normalize(XMVector3Cross(vec_up, vec_tan));

						XMStoreFloat3(&pos_lines[vtx_offset + i], XMLoadFloat3(&pos_curve_pts[i]) + vec_up * curve_plane_height * 0.5f + vec_dir * signed_thickness * 0.5f);
						XMStoreFloat3(&pos_lines[vtx_offset + i + num_pts], XMLoadFloat3(&pos_curve_pts[i]) - vec_up * curve_plane_height * 0.5f + vec_dir * signed_thickness * 0.5f);

						color_lines[vtx_offset + i] = outline_color;
						color_lines[vtx_offset + i + num_pts] = outline_color;

						XMFLOAT3 p = pos_lines[vtx_offset + i];
						aabb._max.x = max(aabb._max.x, p.x);
						aabb._max.y = max(aabb._max.y, p.y);
						aabb._max.z = max(aabb._max.z, p.z);
						aabb._min.x = min(aabb._min.x, p.x);
						aabb._min.y = min(aabb._min.y, p.y);
						aabb._min.z = min(aabb._min.z, p.z);

						p = pos_lines[vtx_offset + i + num_pts];
						aabb._max.x = max(aabb._max.x, p.x);
						aabb._max.y = max(aabb._max.y, p.y);
						aabb._max.z = max(aabb._max.z, p.z);
						aabb._min.x = min(aabb._min.x, p.x);
						aabb._min.y = min(aabb._min.y, p.y);
						aabb._min.z = min(aabb._min.z, p.z);

						if (i < num_pts - 1)
						{
							idx_lines[idx_offset + 2 * i + 0] = vtx_offset + i;
							idx_lines[idx_offset + 2 * i + 1] = vtx_offset + i + 1;

							idx_lines[idx_offset + 2 * i + 0 + 2 * (num_pts - 1)] = vtx_offset + i + num_pts;
							idx_lines[idx_offset + 2 * i + 1 + 2 * (num_pts - 1)] = vtx_offset + i + 1 + num_pts;
						}
					}
					idx_lines[idx_offset + 4 * (num_pts - 1) + 0] = vtx_offset + 0;
					idx_lines[idx_offset + 4 * (num_pts - 1) + 1] = vtx_offset + num_pts;

					idx_lines[idx_offset + 4 * (num_pts - 1) + 2] = vtx_offset + num_pts - 1;
					idx_lines[idx_offset + 4 * (num_pts - 1) + 3] = vtx_offset + 2 * num_pts - 1;

					corners[k * 4 + 0] = vtx_offset + 0;
					corners[k * 4 + 1] = vtx_offset + num_pts;
					corners[k * 4 + 2] = vtx_offset + num_pts - 1;
					corners[k * 4 + 3] = vtx_offset + 2 * num_pts - 1;

					vtx_offset += num_pts * 2;
					idx_offset += num_pts * 2 * 2;
				}

				idx_lines[idx_offset + 0] = corners[0];
				idx_lines[idx_offset + 1] = corners[4 + 0];

				idx_lines[idx_offset + 2] = corners[1];
				idx_lines[idx_offset + 3] = corners[4 + 1];

				idx_lines[idx_offset + 4] = corners[2];
				idx_lines[idx_offset + 5] = corners[4 + 2];

				idx_lines[idx_offset + 6] = corners[3];
				idx_lines[idx_offset + 7] = corners[4 + 3];
			}

			primitive_lines.SetAABB(aabb);
		}

		// Panorama Plane
		{
			const vector<XMFLOAT3>& pos_lines = primitive_lines.GetVtxPositions();

			primitive_panoplane.SetPrimitiveType(PrimitiveType::TRIANGLES);
			vector<XMFLOAT3>& pos_panoplane = primitive_panoplane.GetMutableVtxPositions();
			vector<XMFLOAT3>& nrl_panoplane = primitive_panoplane.GetMutableVtxNormals();
			pos_panoplane.resize(num_pts * 2);
			nrl_panoplane.resize(num_pts * 2);
			vector<uint32_t>& idx_panoplane = primitive_panoplane.GetMutableIdxPrimives();
			idx_panoplane.resize(((num_pts - 1) * 2) * 3);
			primitive_panoplane.SetAABB(primitive_lines.GetAABB());

			//memcpy(pos_panoplane.data(), primitive_lines.GetVtxPositions().data(), sizeof(XMFLOAT3)* pos_panoplane.size());
			memcpy(pos_panoplane.data(), pos_lines.data(), sizeof(XMFLOAT3)* pos_panoplane.size());

			for (size_t i = 0; i < num_pts - 1; ++i)
			{
				idx_panoplane[6 * i + 0] = i;
				idx_panoplane[6 * i + 1] = i + 1;
				idx_panoplane[6 * i + 2] = i + num_pts;

				idx_panoplane[6 * i + 3] = i + 1;
				idx_panoplane[6 * i + 4] = i + 1 + num_pts;
				idx_panoplane[6 * i + 5] = i + num_pts;

				XMVECTOR vec04 = XMLoadFloat3(&pos_panoplane[i + num_pts]) - XMLoadFloat3(&pos_panoplane[i]);
				XMVECTOR vec01 = XMLoadFloat3(&pos_panoplane[i + 1]) - XMLoadFloat3(&pos_panoplane[i]);
				XMVECTOR vec_normal0 = XMVector3Normalize(XMVector3Cross(vec04, vec01));

				XMStoreFloat3(&nrl_panoplane[i], vec_normal0);
				nrl_panoplane[i + num_pts] = nrl_panoplane[i];
			}
			nrl_panoplane[num_pts - 1] = nrl_panoplane[2 * num_pts - 1] = nrl_panoplane[num_pts - 2];
		}

		std::lock_guard<std::recursive_mutex> lock(vzm::GetEngineMutex());
		{
			geometry->MovePrimitivesFrom(std::move(primitives));

			//geometry->MovePrimitiveFrom(std::move(primitives[1]), 0);
			geometry->UpdateRenderData();
		}
		return true;
	}
}
