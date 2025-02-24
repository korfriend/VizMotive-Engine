#include "GComponents.h"
#include "Utils/Backlog.h"

namespace vz
{
	void SlicerComponent::updateCurve()
	{
		isDirtyCurve_ = false;
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

		horizontalCurvePoints_.clear();
		horizontalCurvePoints_.push_back(curve_interpolation.front());

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

				horizontalCurvePoints_.push_back(new_point);

				target_distance += curveInterpolationInterval_;
			}

			accumulated_distance += segment_length;
		}

		// Add the endpoint if the last sampled point is not sufficiently close to the curve's end		
		{
			XMFLOAT3 last_sampled_p = horizontalCurvePoints_.back();
			XMFLOAT3 curve_end_p = curve_interpolation.back();
			float end_distance = XMVectorGetX(XMVector3Length(XMLoadFloat3(&last_sampled_p) - XMLoadFloat3(&curve_end_p)));

			if (end_distance > curveInterpolationInterval_ * 0.5f) {
				horizontalCurvePoints_.push_back(curve_end_p);
			}
		}
	}

	bool SlicerComponent::MakeCurvedSlicerHelperGeometry(const Entity geometryEntity)
	{
		GeometryComponent* geometry = compfactory::GetGeometryComponent(geometryEntity);
		if (geometry == nullptr)
		{
			return false;
		}

		if (isDirtyCurve_)
		{
			updateCurve();
		}

		if (horizontalCurvePoints_.size() == 0)
		{
			vzlog_error("Slicer (%d) has NO curve setting", entity_);
			return false;
		}

		using Primitive = GeometryComponent::Primitive;
		using PrimitiveType = GeometryComponent::PrimitiveType;
		using namespace std;

		vector<Primitive> primitives(3);
		Primitive& primitive_panoplane = primitives[0];
		Primitive& primitive_centerline = primitives[1];
		Primitive& primitive_outline = primitives[2];

		vector<XMFLOAT3>& pos_curve_pts = horizontalCurvePoints_;
		size_t num_pts = pos_curve_pts.size();

		XMVECTOR vec_up = XMLoadFloat3(&up_);
		
		float curve_plane_height = curvedPlaneHeight_;

		geometrics::AABB aabb;

		// Center line
		{
			primitive_centerline.SetPrimitiveType(PrimitiveType::LINES);
			vector<XMFLOAT3>& pos_centerline = primitive_centerline.GetMutableVtxPositions();
			pos_centerline.resize(num_pts);
			vector<uint32_t>& idx_centerline = primitive_centerline.GetMutableIdxPrimives();
			idx_centerline.resize((num_pts - 1) * 2);
			aabb = geometrics::AABB();
			for (size_t i = 0, n = pos_centerline.size(); i < n; ++i)
			{
				XMFLOAT3 p = pos_curve_pts[i];
				pos_centerline[i] = p;

				aabb._max.x = max(aabb._max.x, p.x);
				aabb._max.y = max(aabb._max.y, p.y);
				aabb._max.z = max(aabb._max.z, p.z);
				aabb._min.x = min(aabb._min.x, p.x);
				aabb._min.y = min(aabb._min.y, p.y);
				aabb._min.z = min(aabb._min.z, p.z);

				if (i < num_pts - 1)
				{
					idx_centerline[2 * i + 0] = i;
					idx_centerline[2 * i + 1] = i + 1;
				}
			}
			primitive_centerline.SetAABB(aabb);
		}

		// Out line
		{
			primitive_outline.SetPrimitiveType(PrimitiveType::LINES);
			vector<XMFLOAT3>& pos_outline = primitive_outline.GetMutableVtxPositions();
			pos_outline.resize(num_pts * 2);
			vector<uint32_t>& idx_outline = primitive_outline.GetMutableIdxPrimives();
			idx_outline.resize(num_pts * 2 * 2);
			aabb = geometrics::AABB();
			for (size_t i = 0, n = num_pts; i < n; ++i)
			{
				XMStoreFloat3(&pos_outline[i], XMLoadFloat3(&pos_curve_pts[i]) + vec_up * curve_plane_height * 0.5f);
				XMStoreFloat3(&pos_outline[i + num_pts], XMLoadFloat3(&pos_curve_pts[i]) - vec_up * curve_plane_height * 0.5f);

				XMFLOAT3 p = pos_outline[i];
				aabb._max.x = max(aabb._max.x, p.x);
				aabb._max.y = max(aabb._max.y, p.y);
				aabb._max.z = max(aabb._max.z, p.z);
				aabb._min.x = min(aabb._min.x, p.x);
				aabb._min.y = min(aabb._min.y, p.y);
				aabb._min.z = min(aabb._min.z, p.z);

				p = pos_outline[i + num_pts];
				aabb._max.x = max(aabb._max.x, p.x);
				aabb._max.y = max(aabb._max.y, p.y);
				aabb._max.z = max(aabb._max.z, p.z);
				aabb._min.x = min(aabb._min.x, p.x);
				aabb._min.y = min(aabb._min.y, p.y);
				aabb._min.z = min(aabb._min.z, p.z);

				if (i < num_pts - 1)
				{
					idx_outline[2 * i + 0] = i;
					idx_outline[2 * i + 1] = i + 1;

					idx_outline[2 * i + 0 + 2 * (num_pts - 1)] = i + num_pts;
					idx_outline[2 * i + 1 + 2 * (num_pts - 1)] = i + 1 + num_pts;
				}
			}
			idx_outline[4 * (num_pts - 1) + 0] = 0;
			idx_outline[4 * (num_pts - 1) + 1] = num_pts;

			idx_outline[4 * (num_pts - 1) + 2] = num_pts - 1;
			idx_outline[4 * (num_pts - 1) + 3] = 2 * num_pts - 1;

			primitive_outline.SetAABB(aabb);
		}

		// Panorama Plane
		{
			primitive_panoplane.SetPrimitiveType(PrimitiveType::TRIANGLES);
			vector<XMFLOAT3>& pos_panoplane = primitive_panoplane.GetMutableVtxPositions();
			vector<XMFLOAT3>& nrl_panoplane = primitive_panoplane.GetMutableVtxNormals();
			pos_panoplane.resize(num_pts * 2);
			nrl_panoplane.resize(num_pts * 2);
			vector<uint32_t>& idx_panoplane = primitive_panoplane.GetMutableIdxPrimives();
			idx_panoplane.resize(((num_pts - 1) * 2) * 3);
			primitive_panoplane.SetAABB(primitive_outline.GetAABB());

			memcpy(pos_panoplane.data(), primitive_outline.GetVtxPositions().data(), sizeof(XMFLOAT3)* pos_panoplane.size());

			for (size_t i = 0, n = num_pts; i < n - 1; ++i)
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

		//geometry->MovePrimitivesFrom(std::move(primitives));
		geometry->MovePrimitiveFrom(std::move(primitives[1]), 0);
		geometry->UpdateRenderData();

		return true;
	}
}
