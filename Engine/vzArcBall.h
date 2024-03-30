#pragma once

#include <iostream>
#include <vzMath.h>

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

	class ArcBall
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
				if (!__isPerspective && __np < 0.1f/*float.Epsilon*/)
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
					std::cout << "Arcball Sphere Center is too near to control the arcball - Error!" << std::endl;
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

			matRotatedWS = mat_trs_1 * mat_rot * mat_trs_0;

			if (isInvert)
				angle *= -1;
			return angle;
		}
	};
}