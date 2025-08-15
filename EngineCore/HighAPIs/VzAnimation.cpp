#include "VzEngineAPIs.h"
#include "Components/Components.h"
#include "Utils/Backlog.h"

#define GET_ANI_COMP(COMP, RET) AnimationComponent* COMP = compfactory::GetAnimationComponent(componentVID_); \
	if (!COMP) {post("AnimationFontComponent(" + to_string(componentVID_) + ") is INVALID!", LogLevel::Error); return RET;}

using namespace vz;
using namespace std;
using namespace backlog;

/*
> 하나의 데이터를 여러 채널이 공유할 수도 있지

● 맞습니다!여러 채널이 하나의 AnimationDataComponent를 공유할 수 있습니다.

구조를 다시 보면 :
-AnimationSampler의 data 필드(1724줄)는 wi::ecs::Entity로 AnimationDataComponent를 가리킵니다
- 여러 AnimationSampler가 같은 data 엔티티를 참조할 수 있습니다
- 주석(1734줄)에서도 "The data is now not part of the sampler, so it can be shared"라고 명시되어 있습니다

예를 들어 :
-하나의 AnimationDataComponent에 translation 키프레임 데이터가 있다면
- 여러 채널의 sampler들이 이 같은 데이터를 공유해서 서로 다른 타겟 오브젝트들에 같은 애니메이션을 적용할 수
있습니다

이렇게 설계된 이유는 메모리 효율성과 데이터 재사용을 위해서입니다.
/**/
namespace vzm
{
	// VzAnimation member functions
	void VzAnimation::Play()
	{
		GET_ANI_COMP(comp, );
		comp->Play();
	}

	void VzAnimation::Pause()
	{
		GET_ANI_COMP(comp, );
		comp->Pause();
	}

	void VzAnimation::Stop()
	{
		GET_ANI_COMP(comp, );
		comp->Stop();
	}

	void VzAnimation::Reset()
	{
		GET_ANI_COMP(comp, );
		comp->SetTime(0);
	}

	void VzAnimation::SetTime(float time)
	{
		GET_ANI_COMP(comp, );
		comp->SetTime(time);
	}

	float VzAnimation::GetTime() const
	{
		GET_ANI_COMP(comp, 0.0f);
		return comp->GetTime();
	}

	float VzAnimation::GetDuration() const
	{
		GET_ANI_COMP(comp, 0.0f);
		return comp->GetDuration();
	}

	void VzAnimation::SetSpeed(float speed)
	{
		GET_ANI_COMP(comp, );
		comp->SetSpeed(speed);
	}

	float VzAnimation::GetSpeed() const
	{
		GET_ANI_COMP(comp, 1.0f);
		return comp->GetSpeed();
	}

	void VzAnimation::SetLoopMode(LoopMode mode)
	{
		GET_ANI_COMP(comp, );
		comp->SetLoopMode(static_cast<AnimationComponent::LoopMode>(mode));
	}

	VzAnimation::LoopMode VzAnimation::GetLoopMode() const
	{
		GET_ANI_COMP(comp, LoopMode::EMPTY);
		return static_cast<VzAnimation::LoopMode>(comp->GetLoopMode());
	}
	
	bool VzAnimation::IsPlaying() const
	{
		GET_ANI_COMP(comp, false);
		return comp->IsPlaying();
	}

	bool VzAnimation::IsPaused() const
	{
		GET_ANI_COMP(comp, false);
		return comp->IsPaused();
	}

	bool VzAnimation::IsFinished() const
	{
		GET_ANI_COMP(comp, true);
		return comp->IsFinished();
	}
	
	// VzAnimation::Channel member functions
	void VzAnimation::Channel::AddKeyFrame(const KeyFrame& keyFrame)
	{
		// Implementation depends on how channels are stored in the component
		// This is a placeholder - actual implementation would interact with AnimationComponent
	}

	void VzAnimation::Channel::AddKeyFrame(float time, const vfloat3& value, Interpolation interpolation)
	{
		KeyFrame keyFrame(time, value);
		keyFrame.interpolation = interpolation;
		AddKeyFrame(keyFrame);
	}

	void VzAnimation::Channel::AddKeyFrame(float time, const vfloat4& value, Interpolation interpolation)
	{
		KeyFrame keyFrame(time, value);
		keyFrame.interpolation = interpolation;
		AddKeyFrame(keyFrame);
	}

	void VzAnimation::Channel::RemoveKeyFrame(int index)
	{
		// Implementation would remove keyframe at specified index
	}

	void VzAnimation::Channel::ClearKeyFrames()
	{
		// Implementation would clear all keyframes
	}

	size_t VzAnimation::Channel::GetKeyFrameCount() const
	{
		// Implementation would return number of keyframes
		return 0;
	}

	VzAnimation::KeyFrame VzAnimation::Channel::GetKeyFrame(int index) const
	{
		// Implementation would return keyframe at index
		return KeyFrame();
	}

	std::vector<VzAnimation::KeyFrame> VzAnimation::Channel::GetKeyFrames() const
	{
		// Implementation would return all keyframes
		return std::vector<KeyFrame>();
	}

	void VzAnimation::Channel::SetTargetObject(VID targetObjectVID)
	{
		// Implementation would set target object VID
	}

	VID VzAnimation::Channel::GetTargetObject() const
	{
		// Implementation would return target object VID
		return INVALID_VID;
	}

	VzAnimation::Path VzAnimation::Channel::GetPath() const
	{
		// Implementation would return animation path
		return Path::UNKNOWN;
	}

	void VzAnimation::Channel::SetPath(Path path)
	{
		// Implementation would set animation path
	}

	void VzAnimation::Channel::SetChannelName(const std::string& channelName)
	{
		// Implementation would set channel name
	}

	std::string VzAnimation::Channel::GetChannelName() const
	{
		// Implementation would return channel name
		return "";
	}

	float VzAnimation::Channel::GetDuration() const
	{
		// Implementation would calculate channel duration from keyframes
		return 0.0f;
	}

	vfloat4 VzAnimation::Channel::EvaluateAt(float time) const
	{
		// Implementation would evaluate channel value at given time
		return vfloat4{ 0, 0, 0, 0 };
	}

	void VzAnimation::Channel::SortKeyFramesByTime()
	{
		// Implementation would sort keyframes by time
	}

	// VzAnimation channel management functions
	int VzAnimation::AddChannel(const Path path, const std::string& name, const VID targetVID)
	{
		GET_ANI_COMP(comp, -1);
		return comp->AddChannel(static_cast<AnimationComponent::Path>(path), name, targetVID);
	}

	void VzAnimation::RemoveChannel(const int index)
	{
		GET_ANI_COMP(comp, );
		comp->RemoveChannel(index);
	}

	void VzAnimation::RemoveChannel(const Channel& channel)
	{
		// Implementation would remove specified channel
	}

	void VzAnimation::RemoveChannel(const std::string& name)
	{
		GET_ANI_COMP(comp, );
		comp->RemoveChannel(name);
	}

	void VzAnimation::ClearChannels()
	{
		GET_ANI_COMP(comp, );
		comp->ClearChannels();
	}

	size_t VzAnimation::GetChannelCount() const
	{
		GET_ANI_COMP(comp, 0);
		return comp->GetChannelCount();
	}

	const VzAnimation::Channel& VzAnimation::GetChennel(int index) const
	{
		static Channel dummy;
		// Implementation would return channel at index
		return dummy;
	}

	const VzAnimation::Channel& VzAnimation::GetChennel(const Channel& channel) const
	{
		// Implementation would return specified channel
		return channel;
	}

	const VzAnimation::Channel& VzAnimation::GetChennel(const std::string& name) const
	{
		static Channel dummy;
		// Implementation would return channel by name
		return dummy;
	}

	const std::vector<VzAnimation::Channel*> VzAnimation::GetTracksForTarget(VID targetVID) const
	{
		// Implementation would return channels targeting specified VID
		return std::vector<Channel*>();
	}

	// Object animation utilities
	VzAnimation::Channel* VzAnimation::AddObjectPositionTrack(const ActorVID actorVID)
	{
		AddChannel(Path::TRANSLATION, "Position", actorVID);
		// Return pointer to created channel
		return nullptr;
	}

	VzAnimation::Channel* VzAnimation::AddObjectRotationTrack(const ActorVID actorVID)
	{
		AddChannel(Path::ROTATION, "Rotation", actorVID);
		// Return pointer to created channel
		return nullptr;
	}

	VzAnimation::Channel* VzAnimation::AddObjectScaleTrack(const ActorVID actorVID)
	{
		AddChannel(Path::SCALE, "Scale", actorVID);
		// Return pointer to created channel
		return nullptr;
	}

	// Batch operations for common animation patterns
	void VzAnimation::CreateCameraFlythrough(const CamVID cameraVID, const std::vector<CameraKeyFrame>& keyFrames)
	{
		// Create position and rotation tracks for camera
		Channel* positionTrack = AddObjectPositionTrack(cameraVID);
		Channel* rotationTrack = AddObjectRotationTrack(cameraVID);

		float time = 0.0f;
		float timeStep = 1.0f; // Default 1 second per keyframe
		
		for (const auto& camKeyFrame : keyFrames)
		{
			if (positionTrack)
			{
				positionTrack->AddKeyFrame(time, camKeyFrame.pos);
			}
			
			if (rotationTrack)
			{
				// Convert look direction and up vector to quaternion
				vfloat4 rotation = AnimationUtils::LookRotationToQuaternion(camKeyFrame.view, camKeyFrame.up);
				rotationTrack->AddKeyFrame(time, rotation);
			}
			
			time += timeStep;
		}
	}

	void VzAnimation::CreateCameraOrbit(const CamVID cameraVID, const vfloat3& center, float radius, float duration, int numRevolutions)
	{
		Channel* positionTrack = AddObjectPositionTrack(cameraVID);
		Channel* rotationTrack = AddObjectRotationTrack(cameraVID);

		int numKeyFrames = 36; // 10-degree increments
		float timeStep = duration / (numKeyFrames - 1);
		float totalAngle = 2.0f * 3.14159f * numRevolutions; // 2?* revolutions

		for (int i = 0; i < numKeyFrames; ++i)
		{
			float time = i * timeStep;
			float angle = (totalAngle * i) / (numKeyFrames - 1);

			// Calculate position on orbit
			vfloat3 position = {
				center.x + radius * cosf(angle),
				center.y,
				center.z + radius * sinf(angle)
			};

			// Calculate look direction toward center
			vfloat3 lookDir = {
				center.x - position.x,
				center.y - position.y,
				center.z - position.z
			};
			// Normalize look direction
			float length = sqrtf(lookDir.x * lookDir.x + lookDir.y * lookDir.y + lookDir.z * lookDir.z);
			if (length > 0)
			{
				lookDir.x /= length;
				lookDir.y /= length;
				lookDir.z /= length;
			}

			vfloat3 up = { 0, 1, 0 };
			vfloat4 rotation = AnimationUtils::LookRotationToQuaternion(lookDir, up);

			if (positionTrack)
			{
				positionTrack->AddKeyFrame(time, position);
			}
			if (rotationTrack)
			{
				rotationTrack->AddKeyFrame(time, rotation);
			}
		}
	}

	void VzAnimation::CreateCameraZoom(const CamVID cameraVID, float startFOV, float endFOV, float duration)
	{
		int channelIndex = AddChannel(Path::CAMERA_FOV, "FOV", cameraVID);
		// Implementation would add keyframes for FOV animation
	}

	// Callbacks
	void VzAnimation::SetOnCompleteCallback(std::function<void(VzAnimation*)> callback)
	{
		GET_ANI_COMP(comp, );
		comp->SetOnCompleteCallback([callback, this]() { callback(this); });
	}

	void VzAnimation::SetOnUpdateCallback(std::function<void(VzAnimation*, float)> callback)
	{
		GET_ANI_COMP(comp, );
		comp->SetOnUpdateCallback([callback, this](float deltaTime) { callback(this, deltaTime); });
	}

	// Update function
	void VzAnimation::Update(float deltaTime)
	{
		GET_ANI_COMP(comp, );
		comp->Update(deltaTime);
	}

	// Utility functions
	void VzAnimation::CalculateDuration()
	{
		GET_ANI_COMP(comp, );
		comp->CalculateDuration();
	}

	void VzAnimation::ApplyAnimationToTargets()
	{
		GET_ANI_COMP(comp, );
		comp->ApplyAnimationToTargets();
	}

	// AnimationUtils namespace functions
	namespace AnimationUtils
	{
		float LinearInterpolate(float a, float b, float t)
		{
			return a + (b - a) * t;
		}

		vfloat3 LinearInterpolate(const vfloat3& a, const vfloat3& b, float t)
		{
			return {
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t
			};
		}

		vfloat4 LinearInterpolate(const vfloat4& a, const vfloat4& b, float t)
		{
			return {
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t,
				a.w + (b.w - a.w) * t
			};
		}

		vfloat4 QuaternionSlerp(const vfloat4& a, const vfloat4& b, float t)
		{
			// Calculate dot product
			float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

			// If dot product is negative, invert one quaternion to take shorter path
			vfloat4 b_adjusted = b;
			if (dot < 0.0f)
			{
				dot = -dot;
				b_adjusted.x = -b.x;
				b_adjusted.y = -b.y;
				b_adjusted.z = -b.z;
				b_adjusted.w = -b.w;
			}

			// If quaternions are very close, use linear interpolation
			if (dot > 0.9995f)
			{
				return LinearInterpolate(a, b_adjusted, t);
			}

			// Calculate slerp
			float theta = acosf(dot);
			float sinTheta = sinf(theta);
			float weight1 = sinf((1.0f - t) * theta) / sinTheta;
			float weight2 = sinf(t * theta) / sinTheta;

			return {
				a.x * weight1 + b_adjusted.x * weight2,
				a.y * weight1 + b_adjusted.y * weight2,
				a.z * weight1 + b_adjusted.z * weight2,
				a.w * weight1 + b_adjusted.w * weight2
			};
		}

		vfloat4 CubicSplineInterpolate(const vfloat4& p0, const vfloat4& p1, const vfloat4& p2, const vfloat4& p3, float t)
		{
			float t2 = t * t;
			float t3 = t2 * t;

			// Cubic spline coefficients
			float c0 = -0.5f * t3 + t2 - 0.5f * t;
			float c1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
			float c2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
			float c3 = 0.5f * t3 - 0.5f * t2;

			return {
				c0 * p0.x + c1 * p1.x + c2 * p2.x + c3 * p3.x,
				c0 * p0.y + c1 * p1.y + c2 * p2.y + c3 * p3.y,
				c0 * p0.z + c1 * p1.z + c2 * p2.z + c3 * p3.z,
				c0 * p0.w + c1 * p1.w + c2 * p2.w + c3 * p3.w
			};
		}

		vfloat4 LookRotationToQuaternion(const vfloat3& viewDir, const vfloat3& up)
		{
			// Normalize view direction
			vfloat3 forward = viewDir;
			float length = sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
			if (length > 0)
			{
				forward.x /= length;
				forward.y /= length;
				forward.z /= length;
			}

			// Calculate right vector (cross product of up and forward)
			vfloat3 right = {
				up.y * forward.z - up.z * forward.y,
				up.z * forward.x - up.x * forward.z,
				up.x * forward.y - up.y * forward.x
			};
			length = sqrtf(right.x * right.x + right.y * right.y + right.z * right.z);
			if (length > 0)
			{
				right.x /= length;
				right.y /= length;
				right.z /= length;
			}

			// Calculate corrected up vector
			vfloat3 upCorrected = {
				forward.y * right.z - forward.z * right.y,
				forward.z * right.x - forward.x * right.z,
				forward.x * right.y - forward.y * right.x
			};

			// Build rotation matrix and convert to quaternion
			float trace = right.x + upCorrected.y + forward.z;
			vfloat4 quat;

			if (trace > 0)
			{
				float s = sqrtf(trace + 1.0f) * 2; // s = 4 * qw
				quat.w = 0.25f * s;
				quat.x = (upCorrected.z - forward.y) / s;
				quat.y = (forward.x - right.z) / s;
				quat.z = (right.y - upCorrected.x) / s;
			}
			else if (right.x > upCorrected.y && right.x > forward.z)
			{
				float s = sqrtf(1.0f + right.x - upCorrected.y - forward.z) * 2;
				quat.w = (upCorrected.z - forward.y) / s;
				quat.x = 0.25f * s;
				quat.y = (upCorrected.x + right.y) / s;
				quat.z = (forward.x + right.z) / s;
			}
			else if (upCorrected.y > forward.z)
			{
				float s = sqrtf(1.0f + upCorrected.y - right.x - forward.z) * 2;
				quat.w = (forward.x - right.z) / s;
				quat.x = (upCorrected.x + right.y) / s;
				quat.y = 0.25f * s;
				quat.z = (forward.y + upCorrected.z) / s;
			}
			else
			{
				float s = sqrtf(1.0f + forward.z - right.x - upCorrected.y) * 2;
				quat.w = (right.y - upCorrected.x) / s;
				quat.x = (forward.x + right.z) / s;
				quat.y = (forward.y + upCorrected.z) / s;
				quat.z = 0.25f * s;
			}

			return quat;
		}

		void QuaternionToLookRotation(const vfloat4& quaternion, vfloat3& viewDir, vfloat3& up)
		{
			// Convert quaternion to rotation matrix and extract forward and up vectors
			float x2 = quaternion.x * 2.0f;
			float y2 = quaternion.y * 2.0f;
			float z2 = quaternion.z * 2.0f;
			float xx = quaternion.x * x2;
			float yy = quaternion.y * y2;
			float zz = quaternion.z * z2;
			float xy = quaternion.x * y2;
			float xz = quaternion.x * z2;
			float yz = quaternion.y * z2;
			float wx = quaternion.w * x2;
			float wy = quaternion.w * y2;
			float wz = quaternion.w * z2;

			// Forward vector (third column of rotation matrix)
			viewDir.x = xz + wy;
			viewDir.y = yz - wx;
			viewDir.z = 1.0f - (xx + yy);

			// Up vector (second column of rotation matrix)
			up.x = xy - wz;
			up.y = 1.0f - (xx + zz);
			up.z = yz + wx;
		}

		float WrapTime(float time, float duration, LoopMode loopMode)
		{
			if (duration <= 0.0f) return 0.0f;

			uint32_t modeFlags = static_cast<uint32_t>(loopMode);

			// Check if looped
			if (modeFlags & static_cast<uint32_t>(LoopMode::LOOPED))
			{
				if (modeFlags & static_cast<uint32_t>(LoopMode::PING_PONG))
				{
					// Ping-pong loop
					float normalizedTime = fmodf(time, duration * 2.0f);
					if (normalizedTime > duration)
					{
						return duration * 2.0f - normalizedTime;
					}
					return normalizedTime;
				}
				else
				{
					// Regular loop
					return fmodf(time, duration);
				}
			}
			else
			{
				// Clamp to duration
				if (time < 0.0f) return 0.0f;
				if (time > duration) return duration;
				return time;
			}
		}

		float EaseInOut(float t)
		{
			if (t < 0.5f)
			{
				return 2.0f * t * t;
			}
			else
			{
				return -1.0f + (4.0f - 2.0f * t) * t;
			}
		}

		float EaseIn(float t)
		{
			return t * t;
		}

		float EaseOut(float t)
		{
			return t * (2.0f - t);
		}
	}
	/**/
}