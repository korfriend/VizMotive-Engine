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

		if (camera->IsSlicer())
		{
			((SlicerComponent*)camera)->SetWorldLookTo(*(XMFLOAT3*)&pos, *(XMFLOAT3*)&view, *(XMFLOAT3*)&up);
		}
		else
		{
			camera->SetWorldLookTo(*(XMFLOAT3*)&pos, *(XMFLOAT3*)&view, *(XMFLOAT3*)&up);
		}

		camera->UpdateMatrix();
		UpdateTimeStamp();
	}
	void VzCamera::SetOrthogonalProjection(const float width, const float height, const float zNearP, const float zFarP, const float orthoVerticalSize)
	{
		GET_CAM_COMP(camera, );

		if (camera->IsSlicer())
		{
			((SlicerComponent*)camera)->SetOrtho(width, height, zNearP, zFarP, orthoVerticalSize);
		}
		else
		{
			camera->SetOrtho(width, height, zNearP, zFarP, orthoVerticalSize);
		}

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


		//if (camera->IsSlicer())
		//{
		//	((SlicerComponent*)camera)->SetPerspective(aspectRatio, 1.f, zNearP, zFarP, XMConvertToRadians(isVertical ? fovInDegree : fovInDegree / aspectRatio));
		//}
		//else
		//{
		//	camera->SetPerspective(aspectRatio, 1.f, zNearP, zFarP, XMConvertToRadians(isVertical ? fovInDegree : fovInDegree / aspectRatio));
		//}
		camera->SetPerspective(aspectRatio, 1.f, zNearP, zFarP, XMConvertToRadians(isVertical ? fovInDegree : fovInDegree / aspectRatio));

		camera->UpdateMatrix();
		UpdateTimeStamp();
	}
	void VzCamera::SetIntrinsicsProjection(const float width, const float height, const float nearP, const float farP, const float fx, const float fy, const float cx, const float cy, const float s)
	{
		GET_CAM_COMP(camera, );

		if (camera->GetComponentType() == ComponentType::SLICER)
		{
			vzlog_assert(0, "Slicer does NOT allow intrinsics setting");
			return;
		}
		camera->SetIntrinsicsProjection(width, height, nearP, farP, fx, fy, cx, cy, s);

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
		if (camera->IsOrtho() || camera->IsIntrinsicsProjection())
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
	void VzCamera::GetIntrinsicsProjection(float* zNearP, float* zFarP, float* farP, float* fx, float* fy, float* cx, float* cy, float* sc) const
	{
		GET_CAM_COMP(camera, );
		if (!camera->IsIntrinsicsProjection())
		{
			return;
		}
		float fx0, fy0, cx0, cy0, sc0;
		float w, h, near_p, far_p;
		camera->GetWidthHeight(&w, &h);
		camera->GetNearFar(&near_p, &far_p);
		camera->GetIntrinsics(&fx0, &fy0, &cx0, &cy0, &sc0);

		if (zNearP) *zNearP = near_p;
		if (zFarP) *zFarP = far_p;
		if (fx) *fx = fx0;
		if (fy) *fy = fy0;
		if (cx) *cx = cx0;
		if (cy) *cy = cy0;
		if (sc) *sc = sc0;
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

	void VzCamera::GetViewMatrix(vfloat4x4& view, const bool rowMajor) const
	{
		GET_CAM_COMP(camera, );
		const XMFLOAT4X4& V = camera->GetView();
		if (rowMajor)
		{
			*(XMFLOAT4X4*)&view = V;
		}
		else
		{
			XMFLOAT4X4 V_T;
			XMStoreFloat4x4(&V_T, XMMatrixTranspose(XMLoadFloat4x4(&V)));
			*(XMFLOAT4X4*)&view = V_T;
		}
	}
	void VzCamera::GetProjectionMatrix(vfloat4x4& proj, const bool rowMajor) const
	{
		GET_CAM_COMP(camera, );
		const XMFLOAT4X4& P = camera->GetProjectionJitterFree();
		if (rowMajor)
		{
			*(XMFLOAT4X4*)&proj = P;
		}
		else
		{
			XMFLOAT4X4 P_T;
			XMStoreFloat4x4(&P_T, XMMatrixTranspose(XMLoadFloat4x4(&P)));
			*(XMFLOAT4X4*)&proj = P_T;
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

	bool VzCamera::IsSetByInstrinsics() const 
	{
		GET_CAM_COMP(camera, false);
		return camera->IsIntrinsicsProjection();
	}
	
	void VzCamera::EnableClipper(const bool clipBoxEnabled, const bool clipPlaneEnabled)
	{
		GET_CAM_COMP(camera, );
		camera->SetClipperEnabled(clipBoxEnabled, clipPlaneEnabled);
		UpdateTimeStamp();
	}
	void VzCamera::SetClipPlane(const vfloat4& clipPlane)
	{
		GET_CAM_COMP(camera, );
		camera->SetClipPlane(*(XMFLOAT4*)&clipPlane);
		UpdateTimeStamp();
	}
	void VzCamera::SetClipBox(const vfloat4x4& clipBox)
	{
		GET_CAM_COMP(camera, );
		camera->SetClipBox(*(XMFLOAT4X4*)&clipBox);
		UpdateTimeStamp();
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

	void VzCamera::SetDVRType(const DVR_TYPE type)
	{
		GET_CAM_COMP(camera, );
		camera->SetDVRType((CameraComponent::DVR_TYPE)type);
		UpdateTimeStamp();
	}

	VzCamera::DVR_TYPE VzCamera::GetDVRType() const
	{
		GET_CAM_COMP(camera, VzCamera::DVR_TYPE::DEFAULT);
		return (VzCamera::DVR_TYPE)camera->GetDVRType();
	}

	void VzCamera::SetDVRLookupSlot(const LookupTableSlot slot)
	{
		GET_CAM_COMP(camera, );
		camera->SetDVRLookupSlot((MaterialComponent::LookupTableSlot)slot);
		UpdateTimeStamp();
	}

	LookupTableSlot VzCamera::GetDVRLookupSlot() const
	{
		GET_CAM_COMP(camera, LookupTableSlot::LOOKUP_OTF);
		return (LookupTableSlot)camera->GetDVRLookupSlot();
	}
}

static void compute_screen_matrix(XMMATRIX& ps2ss, const float w, const float h)
{
	XMMATRIX matTranslate = XMMatrixTranslation(1.f, -1.f, 0.f);
	XMMATRIX matScale = XMMatrixScaling(0.5f * w, -0.5f * h, 1.f);

	XMMATRIX matTranslateSampleModel = XMMatrixTranslation(-0.5f, -0.5f, 0.f);

	ps2ss = matTranslate * matScale * matTranslateSampleModel;
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

			ArcBall() = default;
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

		void Initialize(const RendererVID rendererVid, const vfloat3& stageCenter, const float stageRadius) override;
		bool Start(const vfloat2& pos, const float sensitivity = 1.0f) override;
		bool Move(const vfloat2& pos) override;
		bool PanMove(const vfloat2& pos) override;
		bool Zoom(const float zoomDelta, const float sensitivity) override;
	};

	VzCamera::VzCamera(const VID vid, const std::string& originFrom)
		: VzSceneObject(vid, originFrom, COMPONENT_TYPE::CAMERA)
	{
		orbitControl_ = make_unique<OrbitalControlDetail>();
		OrbitalControlDetail* orbitControl_detail = (OrbitalControlDetail*)orbitControl_.get();
		orbitControl_detail->cameraVid = vid;
		orbitControl_detail->vzCamera = this;
	}

	void OrbitalControlDetail::Initialize(const RendererVID rendererVid, const vfloat3& stageCenter, const float stageRadius)
	{
		this->rendererVid = rendererVid;

		arcball::ArcBall& arc_ball = *(arcball::ArcBall*)arcball_.get();
		XMVECTOR stage_center = XMLoadFloat3((XMFLOAT3*)&stageCenter);
		arc_ball.FitArcballToSphere(stage_center, stageRadius);
	}
	bool OrbitalControlDetail::Start(const vfloat2& pos, const float sensitivity)
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
	bool OrbitalControlDetail::Move(const vfloat2& pos)
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

		if (XMVectorGetX(XMVector3LengthSq(view_v)) < 0.000001)
		{
			vzlog_warning("Unexpected drag!");
			return false;
		}

		// Set Pose //...
		XMFLOAT3 eye, view, up;
		XMStoreFloat3(&eye, eye_v);
		XMStoreFloat3(&view, XMVector3Normalize(view_v));
		XMStoreFloat3(&up, XMVector3Normalize(up_v));
		vzCamera->SetWorldPose(__FC3 eye, __FC3 view, __FC3 up);

		return true;
	}
	bool OrbitalControlDetail::PanMove(const vfloat2& pos)
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
			if (vzCamera->IsSetByInstrinsics())
			{
				// forward / backward
				XMFLOAT3 eye, view, up;
				vzCamera->GetWorldPose(*(vfloat3*)&eye, *(vfloat3*)&view, *(vfloat3*)&up);
				XMStoreFloat3(&eye, (XMLoadFloat3(&eye) + (zoomDelta > 0 ? sensitivity : -sensitivity) * XMLoadFloat3(&view)));
				vzCamera->SetWorldPose(__FC3 eye, __FC3 view, __FC3 up);
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
		}
		return true;
	}
}

namespace vzm
{
#define GET_SLICER_COMP(COMP, RET) SlicerComponent* COMP = compfactory::GetSlicerComponent(componentVID_); \
	if (!COMP) {post("CameraComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

	struct SlicerControlDetail : SlicerControl
	{
		CamVID slicerVid = INVALID_VID;
		RendererVID rendererVid = INVALID_VID;
		VzSlicer* vzSlicer = nullptr;

		XMFLOAT3 stageCenter;
		bool isInitialized = false;

		XMFLOAT3 camPos;
		XMFLOAT3 view;
		XMFLOAT3 up;

		float np, fp;
		XMMATRIX matWS2SS;
		XMMATRIX matSS2WS;
		XMMATRIX ws2cs, cs2ps, ps2ss;

		float w, h;
		XMFLOAT2 pos0;
		float orthoVerticalSize = 0;

		float zoomSensitivity = 1.f;
		bool isStartSlicer = false;

		~SlicerControlDetail() = default;

		void Initialize(const RendererVID rendererVid, const vfloat3& stageCenter) override
		{
			this->rendererVid = rendererVid;
			this->stageCenter = *(XMFLOAT3*)&stageCenter;
			this->isInitialized = true;
		}
		bool Start(const vfloat2& pos, const float sensitivity = 1.0f) override
		{
			if (!this->isInitialized)
			{
				backlog::post("SlicerControl::Start >> Not intialized!", backlog::LogLevel::Error);
				return false;
			}

			SlicerComponent* cam = compfactory::GetSlicerComponent(slicerVid);
			TransformComponent* transform = compfactory::GetTransformComponent(slicerVid);
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

			this->camPos = cam->GetWorldEye();
			XMStoreFloat3(&this->view, XMVector3Normalize(XMLoadFloat3(&cam->GetWorldAt()) - XMLoadFloat3(&cam->GetWorldEye())));
			this->up = cam->GetWorldUp();

			this->ws2cs = XMLoadFloat4x4(&cam->GetView());

			cam->GetNearFar(&this->np, &this->fp);

			cam->GetWidthHeight(&this->w, &this->h);

			this->cs2ps = VZMatrixOrthographic(this->w, this->h, this->np, this->fp);
			compute_screen_matrix(this->ps2ss, canvas_w, canvas_h);
			this->matWS2SS = this->ws2cs * this->cs2ps * ps2ss;
			this->matSS2WS = XMMatrixInverse(NULL, this->matWS2SS);

			this->pos0 = *(XMFLOAT2*)&pos;
			this->orthoVerticalSize = cam->GetOrthoVerticalSize();
			this->zoomSensitivity = sensitivity; // heuristic!
			this->isStartSlicer = true;
			return true;
		}
		bool Zoom(const vfloat2& pos, const bool convertZoomdir, const bool preserveStageCenter) override
		{
			if (!this->isInitialized || !this->isStartSlicer)
			{
				backlog::post("SlicerControl::Zoom >> Not intialized!", backlog::LogLevel::Error);
				return false;
			}

			double diff_yss = convertZoomdir ? this->pos0.y - pos.y : pos.y - this->pos0.y;
			if (diff_yss < 0) // zoomout
			{
				this->orthoVerticalSize *= 1.1f * this->zoomSensitivity;
			}
			else
			{
				this->orthoVerticalSize *= 0.9f * this->zoomSensitivity;
			}
			this->pos0.y = pos.y;

			vzSlicer->SetOrthogonalProjection(this->w, this->h, this->orthoVerticalSize);

			if (preserveStageCenter) {
				// https://github.com/korfriend/OsstemCoreAPIs/discussions/146#discussion-4329688
				XMMATRIX& mat_ws2ss = this->matWS2SS;
				XMMATRIX& mat_ss2ws = this->matSS2WS;

				XMVECTOR prev_center_slicer_ss = XMVector3TransformCoord(XMLoadFloat3(&this->stageCenter), mat_ws2ss);

				SlicerComponent* cam = compfactory::GetSlicerComponent(slicerVid);
				float new_w, new_h;
				cam->GetWidthHeight(&new_w, &new_h);
				// new 
				XMMATRIX cs2ps_new = VZMatrixOrthographicOffCenter(-new_w * 0.5f, new_w * 0.5f, -new_h * 0.5f, new_h * 0.5f, this->np, this->fp);
				XMMATRIX mat_ws2ss_new = this->ws2cs * cs2ps_new * this->ps2ss; // note that XMath is based on row major
				XMVECTOR new_center_slicer_ss = XMVector3TransformCoord(XMLoadFloat3(&this->stageCenter), mat_ws2ss_new);

				// always orthogonal projection mode
				XMVECTOR diffSS = new_center_slicer_ss - prev_center_slicer_ss;
				XMVectorSetZ(diffSS, 0);
				XMMATRIX mat_ss2ws_new = XMMatrixInverse(nullptr, mat_ws2ss_new);
				XMVECTOR diffWS = XMVector3TransformNormal(diffSS, mat_ss2ws_new);

				XMFLOAT3 new_pos;
				XMStoreFloat3(&new_pos, XMLoadFloat3(&this->camPos) + diffWS);
				vzSlicer->SetWorldPose(__FC3 new_pos, __FC3 this->view, __FC3 this->up);
			}
			return true;
		}
		bool PanMove(const vfloat2& pos) override
		{
			if (!this->isInitialized || !this->isStartSlicer)
			{
				backlog::post("SlicerControl::PanMove >> Not intialized!", backlog::LogLevel::Error);
				return false;
			}

			XMMATRIX& mat_ws2ss = this->matWS2SS;
			XMMATRIX& mat_ss2ws = this->matSS2WS;

			vzlog_assert(vzSlicer->IsOrtho(), "SlicerControl must be performed under Orthogonal projection")
			{
				XMFLOAT3 pos_prev_ss = XMFLOAT3(this->pos0.x, this->pos0.y, 0);
				XMVECTOR pos_prev_eye_ws = XMVector3TransformCoord(XMLoadFloat3(&pos_prev_ss), mat_ss2ws);
				XMFLOAT3 pos_ss = XMFLOAT3(pos.x, pos.y, 0);
				XMVECTOR pos_cur_eye_ws = XMVector3TransformCoord(XMLoadFloat3(&pos_ss), mat_ss2ws);

				XMVECTOR diff_ws = pos_cur_eye_ws - pos_prev_eye_ws;
				XMVECTOR pos_eye = XMLoadFloat3(&this->camPos) - diff_ws;

				XMFLOAT3 eye;
				XMStoreFloat3(&eye, pos_eye);
				vzSlicer->SetWorldPose(__FC3 eye, __FC3 this->view, __FC3 this->up);
			}
			
			return true;
		}
		bool Move(const float zoomDelta, const float sensitivity) override
		{
			if (!this->isInitialized)
			{
				backlog::post("SlicerControl::Move >> Not intialized!", backlog::LogLevel::Error);
				return false;
			}
			vfloat3 pos0, v0, up0;
			vzSlicer->GetWorldPose(pos0, v0, up0);
			XMVECTOR xm_pos0 = XMLoadFloat3((XMFLOAT3*)&pos0);
			XMVECTOR xm_v0 = XMLoadFloat3((XMFLOAT3*)&v0);
			XMVECTOR new_xm_pos = xm_pos0 + xm_v0 * zoomDelta * sensitivity;
			vfloat3 pos_new;
			XMStoreFloat3((XMFLOAT3*)&pos_new, new_xm_pos);
			vzSlicer->SetWorldPose(pos_new, v0, up0);
			return true;
		}
	};

	VzSlicer::VzSlicer(const VID vid, const std::string& originFrom) : VzCamera(vid, originFrom) 
	{
		type_ = COMPONENT_TYPE::SLICER;

		slicerControl_ = make_unique<SlicerControlDetail>();
		SlicerControlDetail* slicerControl_detail = (SlicerControlDetail*)slicerControl_.get();
		slicerControl_detail->slicerVid = vid;
		slicerControl_detail->vzSlicer = this;
	}

	void VzSlicer::SetCurvedPlaneUp(const vfloat3& up)
	{
		GET_SLICER_COMP(slicer, );
		slicer->SetCurvedPlaneUp(*(XMFLOAT3*)&up);
		UpdateTimeStamp();
	}

	void VzSlicer::SetHorizontalCurveControls(const std::vector<vfloat3>& controlPts, const float interval)
	{
		GET_SLICER_COMP(slicer, );
		slicer->SetHorizontalCurveControls(*(std::vector<XMFLOAT3>*)&controlPts, interval);
		UpdateTimeStamp();
	}

	void VzSlicer::SetCurvedPlaneHeight(const float value)
	{
		GET_SLICER_COMP(slicer, );
		slicer->SetCurvedPlaneHeight(value);
		UpdateTimeStamp();
	}

	bool VzSlicer::MakeCurvedSlicerHelperGeometry(const GeometryVID vid)
	{
		GET_SLICER_COMP(slicer, false);
		return slicer->MakeCurvedSlicerHelperGeometry(vid);
	}

	void VzSlicer::SetSlicerThickness(const float thickness)
	{
		GET_SLICER_COMP(slicer, );
		slicer->SetThickness(thickness);
		UpdateTimeStamp();
	}
	void VzSlicer::SetOutlineThickness(const float pixels)
	{
		GET_SLICER_COMP(slicer, );
		slicer->SetOutlineThickness(pixels);
		UpdateTimeStamp();
	}
}