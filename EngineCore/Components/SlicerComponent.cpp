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
}
