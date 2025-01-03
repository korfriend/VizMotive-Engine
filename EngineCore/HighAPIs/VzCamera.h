#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzCamera : VzSceneComp
	{
		struct OrbitalControl
		{
			OrbitalControl() {};
			virtual ~OrbitalControl() = default;
			virtual void Initialize(const RendererVID rendererVID, const vfloat3 stageCenter, const float stageRadius) = 0;
			virtual bool Start(const vfloat2 pos, const float sensitivity = 1.0f) = 0;
			virtual bool Move(const vfloat2 pos) = 0;	// target is camera
			virtual bool PanMove(const vfloat2 pos) = 0;
			virtual bool Zoom(const float zoomDelta, const float sensitivity) = 0;
		};
	private:
		std::unique_ptr<OrbitalControl> orbitControl_;
	public:
		VzCamera(const VID vid, const std::string& originFrom);
		// Pose parameters are defined in WS (not local space)
		void SetWorldPoseByHierarchy();
		void SetWorldPose(const vfloat3& pos, const vfloat3& view, const vfloat3& up);
		void SetOrthogonalProjection(const float width, const float height, const float zNearP, const float zFarP, const float orthoVerticalSize = 1);
		void SetPerspectiveProjection(const float zNearP, const float zFarP, const float fovInDegree, const float aspectRatio, const bool isVertical = true);
		void GetWorldPose(vfloat3& pos, vfloat3& view, vfloat3& up) const;
		void GetOrthogonalProjection(float* zNearP, float* zFarP, float* width, float* height, float* orthoVerticalSize) const;
		void GetPerspectiveProjection(float* zNearP, float* zFarP, float* fovInDegree, float* aspectRatio, bool isVertical = true) const;

		float GetNear() const;
		float GetCullingFar() const;
		bool IsOrtho() const;

		void EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled);
		void SetClipPlane(const vfloat4& clipPlane);
		void SetClipBox(const vfloat4x4& clipBox);
		bool IsClipperEnabled(bool* clipBoxEnabled = nullptr, bool* clipPlaneEnabled = nullptr) const;

		OrbitalControl* GetOrbitControl() const { return orbitControl_.get(); }
	};

	using OrbitalControl = VzCamera::OrbitalControl;
}
