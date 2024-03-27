#pragma once

#include <vzMath.h>

namespace arcball
{
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

	class ArcBall
	{
	private:
		XMFLOAT3 __posArcballCenter;   // Rotation Center
		float __radius;   // Arcball Radius
		float __activatedRadius;  // Min(m_dRadius, Center2EyeLength - );

		XMFLOAT3 __posOnSurfaceStart;

		// Statics 
		XMMATRIX __matSS2WS;
		XMFLOAT3 __vecView;
		XMFLOAT3 __posCam;

		CameraState __camStateSetInStart;

		bool __isPerspective = false;
		float __np = 0.1;

		bool __isTrackBall = true; // Otherwise, Plane Coordinate
		bool __isStateSet = false;
		bool __isStartArcball = false;
		bool __isFixRotationAxis = false;
		XMFLOAT3 __rotAxis;

		float __trackballSensitive = 10;

		XMFLOAT3 ComputeTargetPoint(const float dPointX, const float dPointY)
		{
			XMFLOAT3 posOnSurface = XMFLOAT3();

			// ==> Make Function
			// Get Near Plane's Position
			XMFLOAT3 posPointSS = XMFLOAT3(dPointX, dPointY, 0);
			XMFLOAT4 posPointWS_h = __matSS2WS * XMFLOAT4(posPointSS, 1.f);
			XMFLOAT3 posPointWS = posPointWS_h / posPointWS_h.w;

			XMFLOAT3 vecRayDir = __vecView;

			if (__isTrackBall)
			{
				// Use Planar Coordinate
				if (!__isPerspective && __np < 0.1f/*float.Epsilon*/)
				{
					posPointWS = posPointWS + vecRayDir * 0.1f;   // <== Think
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
				XMFLOAT3 v3BA = posPointWS - __posArcballCenter;
				// 2nd compute v*v = a^2 + b^2 + c^2
				float dDotVV = glm::dot(vecRayDir, vecRayDir);
				// 3rd compute (A - B)*v = a`a + b`b + c`c
				float dDotBAV = glm::dot(v3BA, vecRayDir);
				// if there's cross then, 4th compute sqrt for min t
				float dDet = dDotBAV * dDotBAV - dDotVV * (glm::dot(v3BA, v3BA) - __activatedRadius * __activatedRadius);
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
		~ArcBall() {};

		void SetArcBallMovingStyle(const bool bIsTrackBall)
		{
			__isTrackBall = bIsTrackBall;
		}

		XMFLOAT3 GetCenterStage() { return __posArcballCenter; };

		void FitArcballToSphere(const XMFLOAT3& posArcballCenter, const float radius)
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
			__posCam = arcballCamState.posCamera;
			__vecView = arcballCamState.vecView;

			XMFLOAT3 vecCam2Center = __posArcballCenter - __posCam;
			__activatedRadius = min(__radius, (float)glm::length(vecCam2Center) * 0.8);

			if (arcballCamState.isPerspective)
			{
				if (glm::length(vecCam2Center) < arcballCamState.np)
				{
					std::cout << "Arcball Sphere Center is too near to control the arcball - Error!" << std::endl;
					return;
				}
			}

			__posOnSurfaceStart = ComputeTargetPoint(pointX, pointY);
		}

		void FixRotationAxis(const XMFLOAT3& rotationAxis)
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
			matRotatedWS = XMMATRIX(1); // identity

			if (!__isStartArcball)
				return 0;

			XMFLOAT3 posOnSurfaceEnd = ComputeTargetPoint(pointX, pointY);

			XMFLOAT3 vecCenter3SurfStart = __posOnSurfaceStart - __posArcballCenter;
			XMFLOAT3 vecCenter3SurfEnd = posOnSurfaceEnd - __posArcballCenter;

			vecCenter3SurfStart = glm::normalize(vecCenter3SurfStart);
			vecCenter3SurfEnd = glm::normalize(vecCenter3SurfEnd);

			XMFLOAT3 rotDir = glm::cross(vecCenter3SurfStart, vecCenter3SurfEnd);
			bool isInvert = false;
			if (__isFixRotationAxis)
			{
				if (glm::dot(rotDir, __rotAxis) >= 0)
					rotDir = __rotAxis;
				else
				{
					isInvert = true;
					rotDir = -__rotAxis;
				}
			}

			if (isReverseDir)
				rotDir *= -1;
			if (glm::dot(rotDir, rotDir) < DBL_EPSILON)
				return 0;

			float angle = 0;
			if (__isTrackBall)
			{
				rotDir = glm::normalize(rotDir);
				float circumference = glm::pi<float>() * 2.0 * __radius;

				XMFLOAT3 vecStart2End = posOnSurfaceEnd - __posOnSurfaceStart;

				if (__isFixRotationAxis)
				{
					vecStart2End = vecStart2End - glm::dot(vecStart2End, rotDir) * rotDir;
				}

				angle = glm::length(vecStart2End) / circumference * __trackballSensitive;
				if (__isPerspective)
				{
					angle *= glm::length(__posCam - __posArcballCenter) / __np; //500
				}
			}
			else
			{
				//dAngleDeg = Vector3D.AngleBetween(v3VecCenter3SurfStart, v3VecCenter3SurfEnd);
				angle = acos(max(min(glm::dot(vecCenter3SurfStart, vecCenter3SurfEnd), 1.0), -1.0)); // 0 to PI
			}
			if (angle == 0) return 0;

			XMMATRIX mat_rot = glm::rotate(angle, rotDir);
			XMMATRIX mat_trs_1 = glm::translate(__posArcballCenter);
			XMMATRIX mat_trs_0 = glm::translate(-__posArcballCenter);

			matRotatedWS = mat_trs_1 * mat_rot * mat_trs_0;

			if (isInvert)
				angle *= -1;
			return angle;
		}
	};
}