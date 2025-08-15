#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzCamera : VzSceneObject
	{
		enum class DVR_TYPE
		{
			// XRAY_[mode] uses 
			DEFAULT = 0,
			XRAY_AVERAGE,
		};
		struct OrbitalControl
		{
			OrbitalControl() {};
			virtual ~OrbitalControl() = default;
			virtual void Initialize(const RendererVID rendererVID, const vfloat3& stageCenter, const float stageRadius) = 0;
			virtual bool Start(const vfloat2& pos, const float sensitivity = 1.0f) = 0;
			virtual bool Move(const vfloat2& pos) = 0;	// target is camera
			virtual bool PanMove(const vfloat2& pos) = 0;
			virtual bool Zoom(const float zoomDelta, const float sensitivity) = 0; // wheel
		};
	private:
		std::unique_ptr<OrbitalControl> orbitControl_;
	public:
		VzCamera(const VID vid, const std::string& originFrom);
		virtual ~VzCamera() = default;

		// Pose parameters are defined in WS (not local space)
		void SetWorldPoseByHierarchy();
		void SetWorldPose(const vfloat3& pos, const vfloat3& view, const vfloat3& up);
		void SetOrthogonalProjection(const float width, const float height, const float zNearP, const float zFarP, const float orthoVerticalSize = 1);
		void SetPerspectiveProjection(const float zNearP, const float zFarP, const float fovInDegree, const float aspectRatio, const bool isVertical = true);
		void SetIntrinsicsProjection(const float width, const float height, const float nearP, const float farP, const float fx, const float fy, const float cx, const float cy, const float s = 0.f);

		void GetWorldPose(vfloat3& pos, vfloat3& view, vfloat3& up) const;
		void GetOrthogonalProjection(float* zNearP, float* zFarP, float* width, float* height, float* orthoVerticalSize) const;
		void GetPerspectiveProjection(float* zNearP, float* zFarP, float* fovInDegree, float* aspectRatio, bool isVertical = true) const;
		void GetIntrinsicsProjection(float* zNearP, float* zFarP, float* farP, float* fx, float* fy, float* cx, float* cy, float* sc) const;

		void GetViewMatrix(vfloat4x4& view, const bool rowMajor = false) const;
		void GetProjectionMatrix(vfloat4x4& proj, const bool rowMajor = false) const;

		float GetNear() const;
		float GetCullingFar() const;
		bool IsOrtho() const;
		bool IsSetByInstrinsics() const;

		void EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled);
		void SetClipPlane(const vfloat4& clipPlane);
		void SetClipBox(const vfloat4x4& clipBox);
		bool IsClipperEnabled(bool* clipBoxEnabled = nullptr, bool* clipPlaneEnabled = nullptr) const;

		void SetDVRType(const DVR_TYPE type);
		void SetDVRLookupSlot(const LookupTableSlot slot);
		DVR_TYPE GetDVRType() const;
		LookupTableSlot GetDVRLookupSlot() const;

		OrbitalControl* GetOrbitControl() const { return orbitControl_.get(); }
	};

	using OrbitalControl = VzCamera::OrbitalControl;

	struct API_EXPORT VzSlicer : VzCamera
	{
		struct SliceControl
		{
			// the interfaces are based on legacy slicer controls.
			//	e.g., mouse-right-drag handles zoom and mouse-wheel handles camera move for/back-ward
			SliceControl() {};
			virtual ~SliceControl() = default;
			virtual void Initialize(const RendererVID rendererVID, const vfloat3& stageCenter) = 0;
			virtual bool Start(const vfloat2& pos, const float sensitivity = 1.0f) = 0;
			virtual bool Zoom(const vfloat2& pos, const bool convertZoomdir, const bool preserveStageCenter = false) = 0;
			virtual bool PanMove(const vfloat2& pos) = 0;
			virtual bool Move(const float moveDelta, const float sensitivity) = 0; // wheel
		};
	private:
		std::unique_ptr<SliceControl> slicerControl_;
	public:

		VzSlicer(const VID vid, const std::string& originFrom);

		void SetOrthogonalProjection(const float width, const float height, const float orthoVerticalSize = -1) { VzCamera::SetOrthogonalProjection(width, height, 0, 10000.f, orthoVerticalSize); }
		void GetOrthogonalProjection(float* width, float* height, float* orthoVerticalSize) const { VzCamera::GetOrthogonalProjection(nullptr, nullptr, width, height, orthoVerticalSize); }

		void SetSlicerThickness(const float thickness);
		void SetOutlineThickness(const float pixels);

		// Curved slicer attributes
		void SetCurvedPlaneUp(const vfloat3& up);
		void SetHorizontalCurveControls(const std::vector<vfloat3>& controlPts, const float interval);
		void SetCurvedPlaneHeight(const float value);
		bool MakeCurvedSlicerHelperGeometry(const GeometryVID vid);

		SliceControl* GetSlicerControl() const { return slicerControl_.get(); }
	};

	using DVR_TYPE = VzCamera::DVR_TYPE;
	using OrbitalControl = VzCamera::OrbitalControl;
	using SlicerControl = VzSlicer::SliceControl;
}
