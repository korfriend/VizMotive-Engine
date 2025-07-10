#include "GComponents.h"
#include "Utils/Backlog.h"

namespace vz
{
	inline float computeInverseLineardepth(const float lin, const float znear, const float zfar)
	{
		float z_n = ((lin - 2 * zfar) * znear + zfar * lin) / (lin * znear - zfar * lin);
		float z = (z_n + 1) * 0.5f;
		return z;
	}
	float CameraComponent::computeOrthoVerticalSizeFromPerspective(const float dist)
	{
		float z = computeInverseLineardepth(std::abs(dist), zNearP_, zFarP_);
		XMMATRIX P = VZMatrixPerspectiveFov(fovY_, width_ / height_, zFarP_, zNearP_); // reverse zbuffer!
		XMMATRIX Unproj = XMMatrixInverse(nullptr, P);
		XMVECTOR Ptop = XMVector3TransformCoord(XMVectorSet(0, 1, z, 1), Unproj);
		XMVECTOR Pbottom = XMVector3TransformCoord(XMVectorSet(0, -1, z, 1), Unproj);
		return XMVectorGetX(XMVector3Length(Ptop - Pbottom));
	}

	bool CameraComponent::SetWorldLookAtFromHierarchyTransforms()
	{
		TransformComponent* tr_comp = compfactory::GetTransformComponent(entity_);
		HierarchyComponent* parent = compfactory::GetHierarchyComponent(entity_);
		XMMATRIX local;
		if (tr_comp == nullptr)
		{
			if (parent == nullptr)
			{
				return false;
			}
			local = XMMatrixIdentity();
		}
		else
		{
			if (tr_comp->IsDirty())
				tr_comp->UpdateMatrix();
			XMFLOAT4X4 local_f44 = tr_comp->GetLocalMatrix();
			local = XMLoadFloat4x4(&local_f44);
		}

		XMMATRIX parent2ws = XMMatrixIdentity();
		while (parent)
		{
			TransformComponent* transform_parent = compfactory::GetTransformComponent(compfactory::GetEntityByVUID(parent->GetParent()));
			if (transform_parent)
			{
				if (transform_parent->IsDirty())
					transform_parent->UpdateMatrix();
				XMFLOAT4X4 local_f44 = transform_parent->GetLocalMatrix();
				parent2ws *= XMLoadFloat4x4(&local_f44);
			}
			parent = compfactory::GetHierarchyComponent(compfactory::GetEntityByVUID(parent->GetParent()));
		}
		XMFLOAT4X4 mat_world;
		XMStoreFloat4x4(&mat_world, local * parent2ws);
		tr_comp->SetWorldMatrix(mat_world);

		XMFLOAT3 eye_prev = eye_;
		XMFLOAT3 up_prev = up_;
		XMFLOAT3 forward_prev = forward_;

		eye_ = *((XMFLOAT3*)&mat_world._41);
		up_ = vz::math::GetUp(mat_world);
		XMFLOAT3 forward = vz::math::GetForward(mat_world);
		XMVECTOR _At = XMLoadFloat3(&eye_) + XMLoadFloat3(&forward);
		XMStoreFloat3(&at_, _At);
		XMStoreFloat3(&forward_, XMVector3Normalize(XMLoadFloat3(&forward)));

		const float epsilon = 1e-06f;
		if (math::DistanceSquared(eye_, eye_prev) < epsilon &&
			math::DistanceSquared(up_, up_prev) < epsilon &&
			math::DistanceSquared(forward_, forward_prev) < epsilon)
		{
			return false;
		}

		isDirty_ = true;
		timeStampSetter_ = TimerNow;
		return true;
	}

	void CameraComponent::SetWorldLookAt(const XMFLOAT3& eye, const XMFLOAT3& at, const XMFLOAT3& up)
	{
		eye_ = eye; at_ = at; up_ = up; XMStoreFloat3(&forward_, XMVector3Normalize(XMLoadFloat3(&at) - XMLoadFloat3(&eye)));
		isDirty_ = true;

		TransformComponent* tr_comp = compfactory::GetTransformComponent(entity_);
		if (tr_comp)
		{
			XMVECTOR _Eye = XMLoadFloat3(&eye_);
			XMVECTOR _At = XMLoadFloat3(&at_);
			XMVECTOR _Up = XMLoadFloat3(&up_);

			XMVECTOR _Dir = _At - _Eye;
			XMVECTOR _Right = XMVector3Cross(_Dir, _Up);
			_Up = XMVector3Cross(_Right, _Dir);
			_Up = XMVector3Normalize(_Up);

			XMMATRIX _V = VZMatrixLookAt(_Eye, _At, _Up);	// world
			XMMATRIX world = XMMatrixInverse(NULL, _V);

			XMMATRIX parent2ws = XMMatrixIdentity();
			HierarchyComponent* parent = compfactory::GetHierarchyComponent(entity_);
			while (parent)
			{
				TransformComponent* transform_parent = compfactory::GetTransformComponent(compfactory::GetEntityByVUID(parent->GetParent()));
				if (transform_parent)
				{
					if (transform_parent->IsDirty())
						transform_parent->UpdateMatrix();
					XMFLOAT4X4 local_f44 = transform_parent->GetLocalMatrix();
					parent2ws *= XMLoadFloat4x4(&local_f44);
				}
				parent = compfactory::GetHierarchyComponent(compfactory::GetEntityByVUID(parent->GetParent()));
			}

			// world = X * P
			XMMATRIX local = world * XMMatrixInverse(NULL, parent2ws);
			XMFLOAT4X4 mat_local;
			XMStoreFloat4x4(&mat_local, local);
			tr_comp->SetMatrix(mat_local);
		}

		timeStampSetter_ = TimerNow;
	}
	void CameraComponent::SetPerspective(const float width, const float height, const float nearP, const float farP, const float fovY)
	{
		width_ = width; height_ = height; zNearP_ = nearP; zFarP_ = farP; fovY_ = fovY;
		flags_ &= ~ORTHOGONAL;
		flags_ &= ~INTRINSICS_PROJECTION;
		flags_ &= ~CUSTOM_PROJECTION;
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}
	void CameraComponent::SetOrtho(const float width, const float height, const float nearP, const float farP, const float orthoVerticalSize)
	{
		width_ = width / height; height_ = 1.f; zNearP_ = nearP; zFarP_ = farP;

		// NOTE: When this function is called for the first time, 
		//	it will always enter the next branch
		if (!(flags_ & ORTHOGONAL)) // when previous setting was perspective
		{
			if (orthoVerticalSize < 0)
				orthoVerticalSize_ = computeOrthoVerticalSizeFromPerspective(math::Length(eye_));
			else if (orthoVerticalSize > 0)
				orthoVerticalSize_ = orthoVerticalSize;
		}

		flags_ |= ORTHOGONAL;
		flags_ &= ~INTRINSICS_PROJECTION;
		flags_ &= ~CUSTOM_PROJECTION;

		{
			if (orthoVerticalSize > 0)
			{
				orthoVerticalSize_ = orthoVerticalSize;
			}
			width_ *= orthoVerticalSize_;
			height_ = orthoVerticalSize_;
		}
		isDirty_ = true; timeStampSetter_ = TimerNow;
	}

	void CameraComponent::SetIntrinsicsProjection(const float width, const float height, const float nearP, const float farP, const float fx, const float fy, const float cx, const float cy, const float s)
	{
		width_ = width; height_ = height; zNearP_ = nearP; zFarP_ = farP;
		fx_ = fx; fy_ = fy; cx_ = cx; cy_ = cy; sc_ = s;

		// Construct off-center projection matrix (DirectX right-handed coordinate system)
		{   
			float w = width, h = height;

			float FarZ = nearP;
			float NearZ = farP;

			float Height = 2.0f * fy / h;
			float Width = 2.0f * fx / w;
			float fRange = FarZ / (NearZ - FarZ);

			projection_._11 = Width;
			projection_._12 = 0.0f;
			projection_._13 = 0.0f;
			projection_._14 = 0.0f;
				
			projection_._21 = -2.0f * s / w;  // skew Ç×
			projection_._22 = Height;
			projection_._23 = 0.0f; 
			projection_._24 = 0.0f;
				
			projection_._31 = (w - 2.0f * cx) / w;  // cx Ç×
			projection_._32 = (2.0f * cy - h) / h;  // cy Ç×
			projection_._33 = fRange;
			projection_._34 = -1.0f;
				
			projection_._41 = 0.0f;
			projection_._42 = 0.0f;
			projection_._43 = fRange * NearZ;
			projection_._44 = 0.0f;

		}

		flags_ &= ~ORTHOGONAL;
		flags_ &= ~CUSTOM_PROJECTION;
		flags_ |= INTRINSICS_PROJECTION;
		isDirty_ = true;
		timeStampSetter_ = TimerNow;
	}

	void SlicerComponent::SetWorldLookAt(const XMFLOAT3& eye, const XMFLOAT3& at, const XMFLOAT3& up)
	{
		if (IsCurvedSlicer())
		{
			XMFLOAT3 eye_valid = eye;
			if (eye_valid.z * eye_valid.z > 0.0000001f)
			{
				vzlog_warning("Curved Slicer forces to set eye.z to zero!");
			}
			eye_valid.z = 0;
			XMFLOAT3 at_valid;
			XMStoreFloat3(&at_valid, XMLoadFloat3(&eye_valid) + XMVectorSet(0, 0, 1, 0));
			CameraComponent::SetWorldLookAt(eye_valid, at_valid, { 0, 1.f, 0 });
		}
		else
		{
			CameraComponent::SetWorldLookAt(eye, at, up);
		}
	}
	void SlicerComponent::SetPerspective(const float width, const float height, const float nearP, const float farP, const float fovY)
	{
		vzlog_assert(0, "SLICER does NOT support PERSPECTIE PROJECTION!");
	}
	void SlicerComponent::SetIntrinsicsProjection(const float width, const float height, 
		const float nearP, const float farP, const float fx, const float fy, const float cx, const float cy, const float s)
	{
		vzlog_assert(0, "SLICER does NOT support INTRINSICS PROJECTION!");
	}
	void SlicerComponent::SetOrtho(const float width, const float height, const float nearP, const float farP, const float orthoVerticalSize)
	{
		if (nearP != 0 || farP != 10000.f)
		{
			vzlog_warning("Slicer's ORTHO projection is forced to set nearP to 0! (farP is set to 10000.f)")
		}
		CameraComponent::SetOrtho(width, height, 0.f, 10000.f, orthoVerticalSize);
	}

	void CameraComponent::UpdateMatrix()
	{
		if (flags_ != CUSTOM_PROJECTION)
		{
			XMMATRIX P;

			if (IsOrtho())
			{
				float aspect = width_ / height_;
				float ortho_width = orthoVerticalSize_ * aspect;
				float ortho_height = orthoVerticalSize_;
				P = VZMatrixOrthographic(ortho_width, ortho_height, zFarP_, zNearP_); // reverse zbuffer!
			}
			else
			{
				if (IsIntrinsicsProjection())  // reverse zbuffer!
				{
					P = XMLoadFloat4x4(&projection_);
				}
				else
				{
					P = VZMatrixPerspectiveFov(fovY_, width_ / height_, zFarP_, zNearP_); // reverse zbuffer!
				}
			}

			XMStoreFloat4x4(&projectionJitterFree_, P); 

			P = P * XMMatrixTranslation(jitter.x, jitter.y, 0);
			XMStoreFloat4x4(&projection_, P);
		}

		XMVECTOR _Eye = XMLoadFloat3(&eye_);
		XMVECTOR _At = XMLoadFloat3(&at_);
		XMVECTOR _Up = XMLoadFloat3(&up_);

		XMVECTOR _Dir = _At - _Eye;
		XMVECTOR _Right = XMVector3Cross(_Dir, _Up);
		_Up = XMVector3Cross(_Right, _Dir);
		_Up = XMVector3Normalize(_Up);

		XMMATRIX _V = VZMatrixLookAt(_Eye, _At, _Up);
		XMStoreFloat4x4(&view_, _V);

		XMMATRIX _P = XMLoadFloat4x4(&projection_);
		XMMATRIX _InvP = XMMatrixInverse(nullptr, _P);
		XMStoreFloat4x4(&invProjection_, _InvP);

		XMMATRIX _VP = XMMatrixMultiply(_V, _P);
		XMStoreFloat4x4(&view_, _V);
		XMStoreFloat4x4(&viewProjection_, _VP);
		XMMATRIX _InvV = XMMatrixInverse(nullptr, _V);
		XMStoreFloat4x4(&invView_, _InvV);
		XMStoreFloat3x3(&rotationMatrix_, _InvV);
		XMStoreFloat4x4(&invViewProjection_, XMMatrixInverse(nullptr, _VP));

		frustum_.Create(_VP);

		isDirty_ = false;
	}
}