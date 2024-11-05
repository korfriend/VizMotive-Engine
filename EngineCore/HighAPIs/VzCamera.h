#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzCamera : VzSceneComp
	{
		VzCamera(const VID vid, const std::string& originFrom)
			: VzSceneComp(vid, originFrom, COMPONENT_TYPE::CAMERA) {}
		// Pose parameters are defined in WS (not local space)
		void SetWorldPoseByHierarchy();
		void SetWorldPose(const vfloat3& pos, const vfloat3& view, const vfloat3& up);
		void SetPerspectiveProjection(const float zNearP, const float zFarP, const float fovInDegree, const float aspectRatio, const bool isVertical = true);
		void GetWorldPose(vfloat3& pos, vfloat3& view, vfloat3& up);
		void GetPerspectiveProjection(float* zNearP, float* zFarP, float* fovInDegree, float* aspectRatio, bool isVertical = true);

		float GetNear();
		float GetCullingFar();
	};
}
