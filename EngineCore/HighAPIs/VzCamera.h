#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzCamera : VzSceneComp
	{
		struct OrbitalControl
		{
			OrbitalControl() {};
			~OrbitalControl() {};
			virtual void Initialize(const RendererVID rendererVID, const vfloat3 stageCenter, const float stageRadius) = 0;
			virtual bool Start(const vfloat2 pos, const float sensitivity = 1.0f) = 0;
			virtual bool Move(const vfloat2 pos) = 0;	// target is camera
			virtual bool PanMove(const vfloat2 pos) = 0;
		};
	private:
		std::unique_ptr<OrbitalControl> orbitControl_;
	public:
		VzCamera(const VID vid, const std::string& originFrom);
		// Pose parameters are defined in WS (not local space)
		void SetWorldPoseByHierarchy();
		void SetWorldPose(const vfloat3& pos, const vfloat3& view, const vfloat3& up);
		void SetPerspectiveProjection(const float zNearP, const float zFarP, const float fovInDegree, const float aspectRatio, const bool isVertical = true);
		void GetWorldPose(vfloat3& pos, vfloat3& view, vfloat3& up) const;
		void GetPerspectiveProjection(float* zNearP, float* zFarP, float* fovInDegree, float* aspectRatio, bool isVertical = true) const;

		float GetNear() const;
		float GetCullingFar() const;

		OrbitalControl* GetOrbitControl() const { return orbitControl_.get(); }

		// just for feasible test for archive (load/store component states)
		struct VzArchive
		{
			int test;
		};
		void Load(const VzArchive& archive);
		void Store(VzArchive& archive);
	};

	using OrbitalControl = VzCamera::OrbitalControl;
}
