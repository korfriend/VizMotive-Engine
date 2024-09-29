#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzCamera : VzSceneComp
	{
		VzCamera(const VID vid, const std::string& originFrom)
			: VzSceneComp(vid, originFrom, "VzCamera", SCENE_COMPONENT_TYPE::CAMERA) {}
		// Pose parameters are defined in WS (not local space)
		void SetWorldPoseByHierarchy();
		void SetWorldPose(const float pos[3], const float view[3], const float up[3]);
		void SetPerspectiveProjection(const float zNearP, const float zFarP, const float fovInDegree, const float aspectRatio, const bool isVertical = true);
		void GetWorldPose(float pos[3], float view[3], float up[3]);
		void GetPerspectiveProjection(float* zNearP, float* zFarP, float* fovInDegree, float* aspectRatio, bool isVertical = true);

		float GetNear();
		float GetCullingFar();
	};
}
