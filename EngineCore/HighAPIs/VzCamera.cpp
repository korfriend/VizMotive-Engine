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
		//jobsystem::context ctx;
		//jobsystem::Execute(ctx, [&](jobsystem::JobArgs args) {
		GET_CAM_COMP(camera, );
		camera->SetWorldLookTo(*(XMFLOAT3*)&pos, *(XMFLOAT3*)&view, *(XMFLOAT3*)&up);
		camera->UpdateMatrix();
		UpdateTimeStamp();
		//	});
		//jobsystem::Wait(ctx);
	}
	void VzCamera::SetPerspectiveProjection(const float zNearP, const float zFarP, const float fovInDegree, const float aspectRatio, const bool isVertical)
	{
		GET_CAM_COMP(camera, );
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
		if (camera->GetProjectionType() != CameraComponent::Projection::PERSPECTIVE)
		{
			return;
		}
		float aspect, near_p, far_p;
		camera->GetWidthHeight(&aspect, nullptr);
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
			XMVECTOR __posArcballCenter;   // Rotation Center
			float __radius;   // Arcball Radius
			float __activatedRadius;  // Min(m_dRadius, Center2EyeLength - );

			XMVECTOR __posOnSurfaceStart;

			// Statics 
			XMMATRIX __matSS2WS;
			XMVECTOR __vecView;
			XMVECTOR __posCam;

			CameraState __camStateSetInStart;

			bool __isPerspective = false;
			float __np = 0.1f;

			bool __isTrackBall = true; // Otherwise, Plane Coordinate
			bool __isStateSet = false;
			bool __isStartArcball = false;
			bool __isFixRotationAxis = false;
			XMVECTOR __rotAxis;

			float __trackballSensitive = 10;

			XMVECTOR ComputeTargetPoint(const float dPointX, const float dPointY)
			{
				XMVECTOR posOnSurface;

				// ==> Make Function
				// Get Near Plane's Position
				XMVECTOR posPointSS = XMVectorSet(dPointX, dPointY, 0, 1);
				XMVECTOR posPointWS = XMVector3TransformCoord(posPointSS, __matSS2WS);

				XMVECTOR vecRayDir = __vecView;

				if (__isTrackBall)
				{
					// Use Planar Coordinate
					if (!__isPerspective && __np < 0.1f) //float.Epsilon
					{
						posPointWS = posPointWS + XMVectorScale(vecRayDir, 0.1f);   // <== Think
					}

					posOnSurface = posPointWS;
				}
				else
				{
					// Use Sphere Coordinate
					if (__isPerspective)
					{
						vecRayDir = posPointWS - __posCam;
					}

					// Center as B, Ray as A + tv
					// B = m_d3PosArcballCenter
					// A = d3PosPointWS, v = v3VecRayDir

					// 1st compute A - B = (a`, b`, c`)
					XMVECTOR v3BA = posPointWS - __posArcballCenter;
					// 2nd compute v*v = a^2 + b^2 + c^2
					float dDotVV = XMVectorGetX(XMVector3Dot(vecRayDir, vecRayDir));
					// 3rd compute (A - B)*v = a`a + b`b + c`c
					float dDotBAV = XMVectorGetX(XMVector3Dot(v3BA, vecRayDir));
					// if there's cross then, 4th compute sqrt for min t
					float dDet = dDotBAV * dDotBAV - dDotVV * (XMVectorGetX(XMVector3Dot(v3BA, v3BA)) - __activatedRadius * __activatedRadius);
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
			bool __is_set_stage;
			float __start_x, __start_y;

			ArcBall() { __is_set_stage = false; };
			~ArcBall() = default;

			void SetArcBallMovingStyle(const bool bIsTrackBall)
			{
				__isTrackBall = bIsTrackBall;
			}

			XMVECTOR GetCenterStage() { return __posArcballCenter; };

			void FitArcballToSphere(const XMVECTOR& posArcballCenter, const float radius)
			{
				__posArcballCenter = posArcballCenter;
				__radius = radius;
				__isStateSet = true;
			}

			void StartArcball(const float pointX, const float pointY, const CameraState& arcballCamState, float tackballSensitive = 10)
			{
				if (!__isStateSet)
					return;
				__isStartArcball = true;
				__trackballSensitive = tackballSensitive;

				__start_x = pointX;
				__start_y = pointY;

				// Start Setting
				__matSS2WS = arcballCamState.matSS2WS;

				// VXCameraState
				__camStateSetInStart = arcballCamState;
				__isPerspective = arcballCamState.isPerspective;
				__np = arcballCamState.np;
				__posCam = XMLoadFloat3(&arcballCamState.posCamera);
				__vecView = XMLoadFloat3(&arcballCamState.vecView);

				XMVECTOR vecCam2Center = __posArcballCenter - __posCam;
				__activatedRadius = std::min(__radius, XMVectorGetX(XMVector3Length(vecCam2Center)) * 0.8f);

				if (arcballCamState.isPerspective)
				{
					if (XMVectorGetX(XMVector3Length(vecCam2Center)) < arcballCamState.np)
					{
						backlog::post("Arcball Sphere Center is too near to control the arcball", backlog::LogLevel::Error);
						return;
					}
				}

				__posOnSurfaceStart = ComputeTargetPoint(pointX, pointY);
			}

			void FixRotationAxis(const XMVECTOR& rotationAxis)
			{
				__isFixRotationAxis = true;
				__rotAxis = rotationAxis;
			}

			void FreeRotationAxis()
			{
				__isFixRotationAxis = false;
			}

			CameraState GetCameraStateSetInStart()
			{
				return __camStateSetInStart;
			}

			float MoveArcball(XMMATRIX& matRotatedWS, const float pointX, const float pointY, const bool isReverseDir)
			{
				matRotatedWS = XMMatrixIdentity(); // identity

				if (!__isStartArcball)
					return 0;

				XMVECTOR posOnSurfaceEnd = ComputeTargetPoint(pointX, pointY);

				XMVECTOR vecCenter3SurfStart = __posOnSurfaceStart - __posArcballCenter;
				XMVECTOR vecCenter3SurfEnd = posOnSurfaceEnd - __posArcballCenter;

				vecCenter3SurfStart = XMVector3Normalize(vecCenter3SurfStart);
				vecCenter3SurfEnd = XMVector3Normalize(vecCenter3SurfEnd);

				XMVECTOR rotDir = XMVector3Cross(vecCenter3SurfStart, vecCenter3SurfEnd);
				bool isInvert = false;
				if (__isFixRotationAxis)
				{
					if (XMVectorGetX(XMVector3Dot(rotDir, __rotAxis)) >= 0)
						rotDir = __rotAxis;
					else
					{
						isInvert = true;
						rotDir = -__rotAxis;
					}
				}

				if (isReverseDir)
					rotDir *= -1;
				if (XMVectorGetX(XMVector3Dot(rotDir, rotDir)) < DBL_EPSILON)
					return 0;

				float angle = 0;
				if (__isTrackBall)
				{
					rotDir = XMVector3Normalize(rotDir);
					float circumference = XM_PI * 2.0f * __radius;

					XMVECTOR vecStart2End = posOnSurfaceEnd - __posOnSurfaceStart;

					if (__isFixRotationAxis)
					{
						vecStart2End =
							vecStart2End - XMVectorGetX(XMVector3Dot(vecStart2End, rotDir)) * rotDir;
					}

					angle = XMVectorGetX(XMVector3Length(vecStart2End)) / circumference * __trackballSensitive;
					if (__isPerspective)
					{
						angle *= XMVectorGetX(XMVector3Length(__posCam - __posArcballCenter)) / __np; //500
					}
				}
				else
				{
					//dAngleDeg = Vector3D.AngleBetween(v3VecCenter3SurfStart, v3VecCenter3SurfEnd);
					angle = acos(std::max(std::min(XMVectorGetX(XMVector3Dot(vecCenter3SurfStart, vecCenter3SurfEnd)), 1.0f), -1.0f)); // 0 to PI
				}
				if (angle == 0) return 0;

				XMMATRIX mat_rot = XMMatrixRotationAxis(rotDir, angle);
				XMMATRIX mat_trs_1 = XMMatrixTranslationFromVector(__posArcballCenter);
				XMMATRIX mat_trs_0 = XMMatrixTranslationFromVector(-__posArcballCenter);

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

		~OrbitalControlDetail() { arcball_.reset(); }

		void Initialize(const RendererVID rendererVid, const vfloat3 stageCenter, const float stageRadius) override;
		bool Start(const vfloat2 pos, const float sensitivity = 1.0f) override;
		bool Move(const vfloat2 pos) override;
		bool PanMove(const vfloat2 pos) override;
	};


	VzCamera::VzCamera(const VID vid, const std::string& originFrom)
		: VzSceneComp(vid, originFrom, COMPONENT_TYPE::CAMERA)
	{
		orbitControl_ = make_unique<OrbitalControlDetail>();
		OrbitalControlDetail* orbitControl_detail = (OrbitalControlDetail*)orbitControl_.get();
		orbitControl_detail->cameraVid = vid;
	}

	void OrbitalControlDetail::Initialize(const RendererVID rendererVid, const vfloat3 stageCenter, const float stageRadius)
	{
		this->rendererVid = rendererVid;

		arcball::ArcBall& arc_ball = *(arcball::ArcBall*)arcball_.get();
		XMVECTOR _stage_center = XMLoadFloat3((XMFLOAT3*)&stageCenter);
		arc_ball.FitArcballToSphere(_stage_center, stageRadius);
		arc_ball.__is_set_stage = true;
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
		if (!arc_ball.__is_set_stage)
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
		compute_screen_matrix(ps2ss, (float)canvas->GetLogicalWidth(), (float)canvas->GetLogicalHeight());
		cam_pose.matWS2SS = ws2cs * cs2ps * ps2ss;
		cam_pose.matSS2WS = XMMatrixInverse(NULL, cam_pose.matWS2SS);

		arc_ball.StartArcball((float)pos.x, (float)pos.y, cam_pose, 10.f * sensitivity);

		return true;
	}
	bool OrbitalControlDetail::Move(const vfloat2 pos)
	{
		arcball::ArcBall& arc_ball = *(arcball::ArcBall*)arcball_.get();
		if (!arc_ball.__is_set_stage)
		{
			backlog::post("OrbitalControl::Start >> Not intialized!", backlog::LogLevel::Error);
			return false;
		}

		XMMATRIX mat_tr;
		arc_ball.MoveArcball(mat_tr, (float)pos.x, (float)pos.y, true);

		arcball::CameraState cam_pose_begin = arc_ball.GetCameraStateSetInStart();
		XMVECTOR vEye = XMVector3TransformCoord(XMLoadFloat3(&cam_pose_begin.posCamera), mat_tr);
		XMVECTOR vView = XMVector3TransformNormal(XMLoadFloat3(&cam_pose_begin.vecView), mat_tr);
		XMVECTOR vUp = XMVector3TransformNormal(XMLoadFloat3(&cam_pose_begin.vecUp), mat_tr);

		// Set Pose //...
		//VmCamera* vCam = sceneManager.GetVmComp<VmCamera>(cameraVid_);
		//XMFLOAT3 pos, view, up;
		//XMStoreFloat3(&pos, vEye);
		//XMStoreFloat3(&view, XMVector3Normalize(vView));
		//XMStoreFloat3(&up, XMVector3Normalize(vUp));
		//vCam->SetPose(__FP pos, __FP view, __FP up);

		return true;
	}
	bool OrbitalControlDetail::PanMove(const vfloat2 pos)
	{
		arcball::ArcBall& arc_ball = *(arcball::ArcBall*)arcball_.get();
		if (!arc_ball.__is_set_stage)
		{
			backlog::post("OrbitalControl::Start >> Not intialized!", backlog::LogLevel::Error);
			return false;
		}

		arcball::CameraState cam_pose_begin = arc_ball.GetCameraStateSetInStart();

		XMMATRIX& mat_ws2ss = cam_pose_begin.matWS2SS;
		XMMATRIX& mat_ss2ws = cam_pose_begin.matSS2WS;

		//VmCamera* vCam = sceneManager.GetVmComp<VmCamera>(cameraVid_);

		if (!cam_pose_begin.isPerspective)
		{
			XMVECTOR pos_eye_ws = XMLoadFloat3(&cam_pose_begin.posCamera);
			XMVECTOR pos_eye_ss = XMVector3TransformCoord(pos_eye_ws, mat_ws2ss);

			XMFLOAT3 v = XMFLOAT3((float)pos.x - arc_ball.__start_x, (float)pos.y - arc_ball.__start_y, 0);
			XMVECTOR diff_ss = XMLoadFloat3(&v);

			pos_eye_ss = pos_eye_ss - diff_ss; // Think Panning! reverse camera moving
			pos_eye_ws = XMVector3TransformCoord(pos_eye_ss, mat_ss2ws);

			XMFLOAT3 pos;
			XMStoreFloat3(&pos, pos_eye_ws);
			//vCam->SetPose(__FP pos, __FP cam_pose_begin.vecView, __FP cam_pose_begin.vecUp);
		}
		else
		{
			XMFLOAT3 f = XMFLOAT3((float)pos.x, (float)pos.y, 0);
			XMVECTOR pos_cur_ss = XMLoadFloat3(&f);
			f = XMFLOAT3(arc_ball.__start_x, arc_ball.__start_y, 0);
			XMVECTOR pos_old_ss = XMLoadFloat3(&f);
			XMVECTOR pos_cur_ws = XMVector3TransformCoord(pos_cur_ss, mat_ss2ws);
			XMVECTOR pos_old_ws = XMVector3TransformCoord(pos_old_ss, mat_ss2ws);
			XMVECTOR diff_ws = pos_cur_ws - pos_old_ws;

			//if (XMVectorGetX(XMVector3Length(diff_ws)) < DBL_EPSILON)
			//{
			//	vCam->SetPose(__FP cam_pose_begin.posCamera, __FP cam_pose_begin.vecView, __FP cam_pose_begin.vecUp);
			//	return true;
			//}
			//
			////cout << "-----0> " << glm::length(diff_ws) << endl;
			////cout << "-----1> " << pos.x << ", " << pos.y << endl;
			////cout << "-----2> " << arc_ball.__start_x << ", " << arc_ball.__start_y << endl;
			//XMVECTOR pos_center_ws = arc_ball.GetCenterStage();
			//XMVECTOR vec_eye2center_ws = pos_center_ws - XMLoadFloat3(&cam_pose_begin.posCamera);
			//
			//float panningCorrected = XMVectorGetX(XMVector3Length(diff_ws)) * XMVectorGetX(XMVector3Length(vec_eye2center_ws)) / cam_pose_begin.np;
			//
			//diff_ws = XMVector3Normalize(diff_ws);
			//XMVECTOR v = XMLoadFloat3(&cam_pose_begin.posCamera) - XMVectorScale(diff_ws, panningCorrected);
			//XMFLOAT3 pos;
			//XMStoreFloat3(&pos, v);
			//vCam->SetPose(__FP pos, __FP cam_pose_begin.vecView, __FP cam_pose_begin.vecUp);
		}
		return true;
	}
}