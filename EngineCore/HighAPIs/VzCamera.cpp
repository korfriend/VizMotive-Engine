#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Common/RenderPath3D.h"
#include "Utils/Backlog.h"

using namespace vz;
using namespace std;
using namespace backlog;

namespace vzm
{
#define GET_CAM_COMP(COMP, RET) CameraComponent* COMP = compfactory::GetCameraComponent(componentVID_); \
	if (!COMP) {post("CameraComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzCamera::SetWorldPoseByHierarchy()
	{
		GET_CAM_COMP(camera, );
		camera->SetWorldLookAtFromHierarchyTransforms();
		camera->UpdateMatrix();
		UpdateTimeStamp();
	}
	void VzCamera::SetWorldPose(const vfloat3& pos, const vfloat3& view, const vfloat3& up)
	{
		GET_CAM_COMP(camera, );
		camera->SetWorldLookTo(*(XMFLOAT3*)&pos, *(XMFLOAT3*)&view, *(XMFLOAT3*)&up);
		camera->UpdateMatrix();
		UpdateTimeStamp();
	}
	void VzCamera::SetOrthogonalProjection(const float width, const float height, const float zNearP, const float zFarP, const float orthoVerticalSize)
	{
		GET_CAM_COMP(camera, );

		if (camera->GetComponentType() == ComponentType::SLICER)
		{
			if (zNearP != 0)
			{
				vzlog_warning("Slicer normally requires zero-nearplane setting, current setting is %f", zNearP);
			}
		}

		camera->SetOrtho(width, height, zNearP, zFarP, orthoVerticalSize);
		camera->UpdateMatrix();
		UpdateTimeStamp();
	}
	void VzCamera::SetPerspectiveProjection(const float zNearP, const float zFarP, const float fovInDegree, const float aspectRatio, const bool isVertical)
	{
		GET_CAM_COMP(camera, );

		if (camera->GetComponentType() == ComponentType::SLICER)
		{
			vzlog_assert(0, "Slicer does NOT allow perspective setting");
			return;
		}

		camera->SetPerspective(aspectRatio, 1.f, zNearP, zFarP, XMConvertToRadians(isVertical ? fovInDegree : fovInDegree / aspectRatio));
		camera->UpdateMatrix();
		UpdateTimeStamp();
	}
	void VzCamera::GetWorldPose(vfloat3& pos, vfloat3& view, vfloat3& up) const
	{
		GET_CAM_COMP(camera, );
		if (camera->IsDirty())
		{
			camera->UpdateMatrix();
		}
		XMFLOAT3 _eye = camera->GetWorldEye();
		XMFLOAT3 _at = camera->GetWorldAt();
		XMFLOAT3 _up = camera->GetWorldUp();
		XMFLOAT3 _view;
		XMStoreFloat3(&_view, XMLoadFloat3(&_at) - XMLoadFloat3(&_eye));
		*(XMFLOAT3*)&pos = _eye;
		*(XMFLOAT3*)&view = _view;
		*(XMFLOAT3*)&up = _up;
	}
	void VzCamera::GetPerspectiveProjection(float* zNearP, float* zFarP, float* fovInDegree, float* aspectRatio, bool isVertical) const
	{
		GET_CAM_COMP(camera, );
		if (camera->IsOrtho())
		{
			return;
		}
		float aspect, near_p, far_p;
		float w, h;
		camera->GetWidthHeight(&w, &h);
		aspect = w / h;

		camera->GetNearFar(&near_p, &far_p);
		if (fovInDegree)
		{
			*fovInDegree = XMConvertToDegrees(isVertical ? camera->GetFovVertical() : camera->GetFovVertical() / aspect);
		}
		if (aspectRatio)
		{
			*aspectRatio = aspect;
		}
		if (zNearP)
		{
			*zNearP = near_p;
		}
		if (zFarP)
		{
			*zFarP = far_p;
		}
	}

	void VzCamera::GetOrthogonalProjection(float* zNearP, float* zFarP, float* width, float* height, float* orthoVerticalSize) const
	{
		GET_CAM_COMP(camera, );
		if (!camera->IsOrtho())
		{
			return;
		}
		float near_p, far_p;
		float w, h;
		camera->GetWidthHeight(&w, &h);
		camera->GetNearFar(&near_p, &far_p);
		if (width)
		{
			*width = w;
		}
		if (height)
		{
			*height = h;
		}
		if (zNearP)
		{
			*zNearP = near_p;
		}
		if (zFarP)
		{
			*zFarP = far_p;
		}
		if (orthoVerticalSize)
		{
			*orthoVerticalSize = camera->GetOrthoVerticalSize();
		}
	}

	float VzCamera::GetNear() const
	{
		GET_CAM_COMP(camera, -1.f);
		float ret;
		camera->GetNearFar(&ret, nullptr);
		return ret;
	}
	float VzCamera::GetCullingFar() const
	{
		GET_CAM_COMP(camera, -1.f);
		float ret;
		camera->GetNearFar(nullptr, &ret);
		return ret;
	}

	bool VzCamera::IsOrtho() const
	{
		GET_CAM_COMP(camera, false);
		return camera->IsOrtho();
	}
	
	void VzCamera::EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled)
	{
		GET_CAM_COMP(camera, );
		camera->EnableClipper(clipBoxEnabled, clipPlaneEnabled);
	}
	void VzCamera::SetClipPlane(const vfloat4& clipPlane)
	{
		GET_CAM_COMP(camera, );
		camera->SetClipPlane(*(XMFLOAT4*)&clipPlane);
	}
	void VzCamera::SetClipBox(const vfloat4x4& clipBox)
	{
		GET_CAM_COMP(camera, );
		camera->SetClipBox(*(XMFLOAT4X4*)&clipBox);
	}
	bool VzCamera::IsClipperEnabled(bool* clipBoxEnabled, bool* clipPlaneEnabled) const
	{
		GET_CAM_COMP(camera, false);
		bool box_clipped = camera->IsBoxClipperEnabled();
		bool plane_clipped = camera->IsPlaneClipperEnabled();
		if (clipBoxEnabled) *clipBoxEnabled = box_clipped;
		if (clipPlaneEnabled) *clipPlaneEnabled = plane_clipped;
		return box_clipped || plane_clipped;
	}
}

namespace vzm
{
	namespace arcball
	{
		// following row major convention
		struct CameraState
		{
			XMFLOAT3 posCamera;
			XMFLOAT3 vecView;
			XMFLOAT3 vecUp;

			float np;
			bool isPerspective;

			XMMATRIX matSS2WS;
			XMMATRIX matWS2SS;
		};

		struct ArcBall
		{
		private:
			XMVECTOR posArcballCenter_;   // Rotation Center
			float radius_;   // Arcball Radius
			float activatedRadius_;  // Min(m_dRadius, Center2EyeLength - );

			XMVECTOR posOnSurfaceStart_;

			// Statics 
			XMMATRIX matSS2WS_;
			XMVECTOR vecView_;
			XMVECTOR posCam_;

			CameraState camStateSetInStart_;

			bool isPerspective_ = false;
			float np_ = 0.1f;

			bool isTrackBall_ = true; // Otherwise, Plane Coordinate
			bool isStateSet_ = false;
			bool isStartArcball_ = false;
			bool isFixRotationAxis_ = false;
			XMVECTOR rotAxis_;

			float trackballSensitive_ = 10;

			XMVECTOR computeTargetPoint(const float dPointX, const float dPointY)
			{
				XMVECTOR posOnSurface;

				// ==> Make Function
				// Get Near Plane's Position
				XMVECTOR posPointSS = XMVectorSet(dPointX, dPointY, 0, 1);
				XMVECTOR posPointWS = XMVector3TransformCoord(posPointSS, matSS2WS_);

				XMVECTOR vecRayDir = vecView_;

				if (isTrackBall_)
				{
					// Use Planar Coordinate
					if (!isPerspective_ && np_ < 0.1f) //float.Epsilon
					{
						posPointWS = posPointWS + XMVectorScale(vecRayDir, 0.1f);   // <== Think
					}

					posOnSurface = posPointWS;
				}
				else
				{
					// Use Sphere Coordinate
					if (isPerspective_)
					{
						vecRayDir = posPointWS - posCam_;
					}

					// Center as B, Ray as A + tv
					// B = posArcballCenter_
					// A = posPointWS, v = v3VecRayDir

					// 1st compute A - B = (a`, b`, c`)
					XMVECTOR v3BA = posPointWS - posArcballCenter_;
					// 2nd compute v*v = a^2 + b^2 + c^2
					float dDotVV = XMVectorGetX(XMVector3Dot(vecRayDir, vecRayDir));
					// 3rd compute (A - B)*v = a`a + b`b + c`c
					float dDotBAV = XMVectorGetX(XMVector3Dot(v3BA, vecRayDir));
					// if there's cross then, 4th compute sqrt for min t
					float dDet = dDotBAV * dDotBAV - dDotVV * (XMVectorGetX(XMVector3Dot(v3BA, v3BA)) - activatedRadius_ * activatedRadius_);
					float dT;

					if (dDet >= 0)
					{
						dT = -(dDotBAV + sqrt(dDet)) / dDotVV;
					}
					else
					{
						dT = -dDotBAV / dDotVV;
					}

					posOnSurface = posPointWS + vecRayDir * dT;
				}

				return posOnSurface;
			}
		public:
			bool IsSetStage() const { return isStateSet_; }
			float startX, startY;

			ArcBall() {};
			~ArcBall() = default;

			void SetArcBallMovingStyle(const bool bIsTrackBall)
			{
				isTrackBall_ = bIsTrackBall;
			}

			XMVECTOR GetCenterStage() { return posArcballCenter_; };

			void FitArcballToSphere(const XMVECTOR& posArcballCenter, const float radius)
			{
				posArcballCenter_ = posArcballCenter;
				radius_ = radius;
				isStateSet_ = true;
			}

			void StartArcball(const float pointX, const float pointY, const CameraState& arcballCamState, float tackballSensitive = 10)
			{
				if (!isStateSet_)
					return;
				isStartArcball_ = true;
				trackballSensitive_ = tackballSensitive;

				startX = pointX;
				startY = pointY;

				// Start Setting
				matSS2WS_ = arcballCamState.matSS2WS;

				// VXCameraState
				camStateSetInStart_ = arcballCamState;
				isPerspective_ = arcballCamState.isPerspective;
				np_ = arcballCamState.np;
				posCam_ = XMLoadFloat3(&arcballCamState.posCamera);
				vecView_ = XMLoadFloat3(&arcballCamState.vecView);

				XMVECTOR vecCam2Center = posArcballCenter_ - posCam_;
				activatedRadius_ = std::min(radius_, XMVectorGetX(XMVector3Length(vecCam2Center)) * 0.8f);

				if (arcballCamState.isPerspective)
				{
					if (XMVectorGetX(XMVector3Length(vecCam2Center)) < arcballCamState.np)
					{
						backlog::post("Arcball Sphere Center is too near to control the arcball", backlog::LogLevel::Error);
						return;
					}
				}

				posOnSurfaceStart_ = computeTargetPoint(pointX, pointY);
			}

			void FixRotationAxis(const XMVECTOR& rotationAxis)
			{
				isFixRotationAxis_ = true;
				rotAxis_ = rotationAxis;
			}

			void FreeRotationAxis()
			{
				isFixRotationAxis_ = false;
			}

			CameraState GetCameraStateSetInStart()
			{
				return camStateSetInStart_;
			}

			float MoveArcball(XMMATRIX& matRotatedWS, const float pointX, const float pointY, const bool isReverseDir)
			{
				matRotatedWS = XMMatrixIdentity(); // identity

				if (!isStartArcball_)
					return 0;

				XMVECTOR posOnSurfaceEnd = computeTargetPoint(pointX, pointY);

				XMVECTOR vecCenter3SurfStart = posOnSurfaceStart_ - posArcballCenter_;
				XMVECTOR vecCenter3SurfEnd = posOnSurfaceEnd - posArcballCenter_;

				vecCenter3SurfStart = XMVector3Normalize(vecCenter3SurfStart);
				vecCenter3SurfEnd = XMVector3Normalize(vecCenter3SurfEnd);

				XMVECTOR rotDir = XMVector3Cross(vecCenter3SurfStart, vecCenter3SurfEnd);
				bool isInvert = false;
				if (isFixRotationAxis_)
				{
					if (XMVectorGetX(XMVector3Dot(rotDir, rotAxis_)) >= 0)
						rotDir = rotAxis_;
					else
					{
						isInvert = true;
						rotDir = -rotAxis_;
					}
				}

				if (isReverseDir)
					rotDir *= -1;
				if (XMVectorGetX(XMVector3Dot(rotDir, rotDir)) < DBL_EPSILON)
					return 0;

				float angle = 0;
				if (isTrackBall_)
				{
					rotDir = XMVector3Normalize(rotDir);
					float circumference = XM_PI * 2.0f * radius_;

					XMVECTOR vecStart2End = posOnSurfaceEnd - posOnSurfaceStart_;

					if (isFixRotationAxis_)
					{
						vecStart2End =
							vecStart2End - XMVectorGetX(XMVector3Dot(vecStart2End, rotDir)) * rotDir;
					}

					angle = XMVectorGetX(XMVector3Length(vecStart2End)) / circumference * trackballSensitive_;
					if (isPerspective_)
					{
						angle *= XMVectorGetX(XMVector3Length(posCam_ - posArcballCenter_)) / np_; //500
					}
				}
				else
				{
					//dAngleDeg = Vector3D.AngleBetween(v3VecCenter3SurfStart, v3VecCenter3SurfEnd);
					angle = acos(std::max(std::min(XMVectorGetX(XMVector3Dot(vecCenter3SurfStart, vecCenter3SurfEnd)), 1.0f), -1.0f)); // 0 to PI
				}
				if (angle == 0) return 0;

				XMMATRIX mat_rot = XMMatrixRotationAxis(rotDir, angle);
				XMMATRIX mat_trs_1 = XMMatrixTranslationFromVector(posArcballCenter_);
				XMMATRIX mat_trs_0 = XMMatrixTranslationFromVector(-posArcballCenter_);

				matRotatedWS = mat_trs_0 * mat_rot * mat_trs_1;

				if (isInvert)
					angle *= -1;
				return angle;
			}
		};
	}

	struct OrbitalControlDetail : OrbitalControl
	{
		CamVID cameraVid = INVALID_VID;
		RendererVID rendererVid = INVALID_VID;
		std::unique_ptr<arcball::ArcBall> arcball_ = std::make_unique<arcball::ArcBall>();
		VzCamera* vzCamera = nullptr;

		~OrbitalControlDetail() { arcball_.reset(); }

		void Initialize(const RendererVID rendererVid, const vfloat3 stageCenter, const float stageRadius) override;
		bool Start(const vfloat2 pos, const float sensitivity = 1.0f) override;
		bool Move(const vfloat2 pos) override;
		bool PanMove(const vfloat2 pos) override;
		bool Zoom(const float zoomDelta, const float sensitivity) override;
	};

	VzCamera::VzCamera(const VID vid, const std::string& originFrom)
		: VzSceneComp(vid, originFrom, COMPONENT_TYPE::CAMERA)
	{
		orbitControl_ = make_unique<OrbitalControlDetail>();
		OrbitalControlDetail* orbitControl_detail = (OrbitalControlDetail*)orbitControl_.get();
		orbitControl_detail->cameraVid = vid;
		orbitControl_detail->vzCamera = this;
	}

	void OrbitalControlDetail::Initialize(const RendererVID rendererVid, const vfloat3 stageCenter, const float stageRadius)
	{
		this->rendererVid = rendererVid;

		arcball::ArcBall& arc_ball = *(arcball::ArcBall*)arcball_.get();
		XMVECTOR stage_center = XMLoadFloat3((XMFLOAT3*)&stageCenter);
		arc_ball.FitArcballToSphere(stage_center, stageRadius);
	}
	void compute_screen_matrix(XMMATRIX& ps2ss, const float w, const float h)
	{
		XMMATRIX matTranslate = XMMatrixTranslation(1.f, -1.f, 0.f);
		XMMATRIX matScale = XMMatrixScaling(0.5f * w, -0.5f * h, 1.f);

		XMMATRIX matTranslateSampleModel = XMMatrixTranslation(-0.5f, -0.5f, 0.f);

		ps2ss = matTranslate * matScale * matTranslateSampleModel;
	}
	bool OrbitalControlDetail::Start(const vfloat2 pos, const float sensitivity)
	{
		arcball::ArcBall& arc_ball = *(arcball::ArcBall*)arcball_.get();
		if (!arc_ball.IsSetStage())
		{
			backlog::post("OrbitalControl::Start >> Not intialized!", backlog::LogLevel::Error);
			return false;
		}

		CameraComponent* cam = compfactory::GetCameraComponent(cameraVid);
		TransformComponent* transform = compfactory::GetTransformComponent(cameraVid);
		Canvas* canvas = canvas::GetCanvas(rendererVid);
		if (cam == nullptr || transform == nullptr)
		{
			backlog::post("Invalid Camera", backlog::LogLevel::Error);
			return false;
		}
		float canvas_w = (float)canvas->GetLogicalWidth();
		float canvas_h = (float)canvas->GetLogicalHeight();
		if (pos.x < 0 || pos.y < 0 || pos.x >= canvas_w || pos.y >= canvas_h)
		{
			return false;
		}

		// Orbital Camera
		// we assume the transform->world is not dirty state
		XMMATRIX matWorld = XMLoadFloat4x4(&transform->GetWorldMatrix());

		arcball::CameraState cam_pose;
		cam_pose.isPerspective = true;

		float fp;
		cam->GetNearFar(&cam_pose.np, &fp);

		cam_pose.posCamera = cam->GetWorldEye();
		XMStoreFloat3(&cam_pose.vecView, XMLoadFloat3(&cam->GetWorldAt()) - XMLoadFloat3(&cam->GetWorldEye()));
		cam_pose.vecUp = cam->GetWorldUp();

		XMMATRIX ws2cs, cs2ps, ps2ss;
		ws2cs = XMLoadFloat4x4(&cam->GetView());

		float w, h;
		cam->GetWidthHeight(&w, &h);

		float aspect = w / h;
		cs2ps = VZMatrixPerspectiveFov(XM_PIDIV4, aspect, cam_pose.np, fp);
		compute_screen_matrix(ps2ss, canvas_w, canvas_h);
		cam_pose.matWS2SS = ws2cs * cs2ps * ps2ss;
		cam_pose.matSS2WS = XMMatrixInverse(NULL, cam_pose.matWS2SS);

		arc_ball.StartArcball((float)pos.x, (float)pos.y, cam_pose, 10.f * sensitivity);

		return true;
	}
	bool OrbitalControlDetail::Move(const vfloat2 pos)
	{
		arcball::ArcBall& arc_ball = *(arcball::ArcBall*)arcball_.get();
		if (!arc_ball.IsSetStage())
		{
			backlog::post("OrbitalControl::Start >> Not intialized!", backlog::LogLevel::Error);
			return false;
		}

		XMMATRIX mat_tr;
		arc_ball.MoveArcball(mat_tr, (float)pos.x, (float)pos.y, true);

		arcball::CameraState cam_pose_begin = arc_ball.GetCameraStateSetInStart();
		XMVECTOR eye_v = XMVector3TransformCoord(XMLoadFloat3(&cam_pose_begin.posCamera), mat_tr);
		XMVECTOR view_v = XMVector3TransformNormal(XMLoadFloat3(&cam_pose_begin.vecView), mat_tr);
		XMVECTOR up_v = XMVector3TransformNormal(XMLoadFloat3(&cam_pose_begin.vecUp), mat_tr);

		// Set Pose //...
		XMFLOAT3 eye, view, up;
		XMStoreFloat3(&eye, eye_v);
		XMStoreFloat3(&view, XMVector3Normalize(view_v));
		XMStoreFloat3(&up, XMVector3Normalize(up_v));
		vzCamera->SetWorldPose(__FC3 eye, __FC3 view, __FC3 up);

		return true;
	}
	bool OrbitalControlDetail::PanMove(const vfloat2 pos)
	{
		arcball::ArcBall& arc_ball = *(arcball::ArcBall*)arcball_.get();
		if (!arc_ball.IsSetStage())
		{
			backlog::post("OrbitalControl::Start >> Not intialized!", backlog::LogLevel::Error);
			return false;
		}

		arcball::CameraState cam_pose_begin = arc_ball.GetCameraStateSetInStart();

		XMMATRIX& mat_ws2ss = cam_pose_begin.matWS2SS;
		XMMATRIX& mat_ss2ws = cam_pose_begin.matSS2WS;

		if (!cam_pose_begin.isPerspective)
		{
			XMVECTOR pos_eye_ws = XMLoadFloat3(&cam_pose_begin.posCamera);
			XMVECTOR pos_eye_ss = XMVector3TransformCoord(pos_eye_ws, mat_ws2ss);

			XMFLOAT3 v = XMFLOAT3((float)pos.x - arc_ball.startX, (float)pos.y - arc_ball.startY, 0);
			XMVECTOR diff_ss = XMLoadFloat3(&v);

			pos_eye_ss = pos_eye_ss - diff_ss; // Think Panning! reverse camera moving
			pos_eye_ws = XMVector3TransformCoord(pos_eye_ss, mat_ss2ws);

			XMFLOAT3 eye;
			XMStoreFloat3(&eye, pos_eye_ws);
			vzCamera->SetWorldPose(__FC3 eye, __FC3 cam_pose_begin.vecView, __FC3 cam_pose_begin.vecUp);
		}
		else
		{
			XMFLOAT3 f = XMFLOAT3((float)pos.x, (float)pos.y, 0);
			XMVECTOR pos_cur_ss = XMLoadFloat3(&f);
			f = XMFLOAT3(arc_ball.startX, arc_ball.startY, 0);
			XMVECTOR pos_old_ss = XMLoadFloat3(&f);
			XMVECTOR pos_cur_ws = XMVector3TransformCoord(pos_cur_ss, mat_ss2ws);
			XMVECTOR pos_old_ws = XMVector3TransformCoord(pos_old_ss, mat_ss2ws);
			XMVECTOR diff_ws = pos_cur_ws - pos_old_ws;

			if (XMVectorGetX(XMVector3Length(diff_ws)) < DBL_EPSILON)
			{
				vzCamera->SetWorldPose(__FC3 cam_pose_begin.posCamera, __FC3 cam_pose_begin.vecView, __FC3 cam_pose_begin.vecUp);
				return true;
			}

			XMVECTOR pos_center_ws = arc_ball.GetCenterStage();
			XMVECTOR vec_eye2center_ws = pos_center_ws - XMLoadFloat3(&cam_pose_begin.posCamera);

			float panningCorrected = XMVectorGetX(XMVector3Length(diff_ws)) * XMVectorGetX(XMVector3Length(vec_eye2center_ws)) / cam_pose_begin.np;

			diff_ws = XMVector3Normalize(diff_ws);
			XMVECTOR v = XMLoadFloat3(&cam_pose_begin.posCamera) - XMVectorScale(diff_ws, panningCorrected);
			XMFLOAT3 eye;
			XMStoreFloat3(&eye, v);
			vzCamera->SetWorldPose(__FC3 eye, __FC3 cam_pose_begin.vecView, __FC3 cam_pose_begin.vecUp);
		}
		return true;
	}
	bool OrbitalControlDetail::Zoom(const float zoomDelta, const float sensitivity)
	{
		if (vzCamera->IsOrtho())
		{
			float z_near, z_far;
			float w, h;
			float ortho_vertical_size;
			vzCamera->GetOrthogonalProjection(&z_near, &z_far, &w, &h, &ortho_vertical_size);
			if (ortho_vertical_size <= 0)
			{
				backlog::post("invalid orthogonal state!", backlog::LogLevel::Error);
				return false;
			}
			if (zoomDelta > 0) {
				ortho_vertical_size *= 0.9f * sensitivity;
			}
			else {
				ortho_vertical_size *= 1.1f * sensitivity;
			}

			if (ortho_vertical_size < 0.0001)
			{
				backlog::post("invalid Ortho-Size: " + std::to_string(ortho_vertical_size), backlog::LogLevel::Warn);
				return false;
			}

			vzCamera->SetOrthogonalProjection(w, h, z_near, z_far, ortho_vertical_size);
		}
		else
		{
			float z_near, z_far;
			float aspect_ratio, vertical_fov;
			vzCamera->GetPerspectiveProjection(&z_near, &z_far, &vertical_fov, &aspect_ratio, true);
			if (zoomDelta > 0) {
				vertical_fov *= 0.9f * sensitivity;
			}
			else {
				vertical_fov *= 1.1f * sensitivity;
			}

			if (vertical_fov < 0.1 || vertical_fov > 179.9)
			{
				backlog::post("invalid FOV: " + std::to_string(vertical_fov) + "(Deg)", backlog::LogLevel::Warn);
				return false;
			}

			vzCamera->SetPerspectiveProjection(z_near, z_far, vertical_fov, aspect_ratio, true);
		}
		return true;
	}
}

namespace vzm
{
#define GET_SLICER_COMP(COMP, RET) SlicerComponent* COMP = compfactory::GetSlicerComponent(componentVID_); \
	if (!COMP) {post("CameraComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	void VzSlicer::SetHorizontalCurveControls(const std::vector<vfloat3>& controlPts, const float interval)
	{
		GET_SLICER_COMP(slicer, );
		slicer->SetHorizontalCurveControls(*(std::vector<XMFLOAT3>*)&controlPts, interval);
		UpdateTimeStamp();
	}
}