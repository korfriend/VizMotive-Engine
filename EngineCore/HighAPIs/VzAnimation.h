#pragma once
#include "VzComponentAPIs.h"

namespace vzm
{
	struct API_EXPORT VzAnimation : VzBaseComp
	{
		enum class Interpolation : uint32_t
		{
			LINEAR = 0,
			STEP,
			CUBIC_SPLINE,
			MODE_FORCE_UINT32 = 0xFFFFFFFF
		};
		enum class LoopMode : uint32_t
		{
			EMPTY = 0,
			PLAYING = 1 << 0,
			LOOPED = 1 << 1,
			ROOT_MOTION = 1 << 2,
			PING_PONG = 1 << 3,
		};
		enum class Path : uint32_t
		{
			TRANSLATION,
			ROTATION,
			SCALE,
			WEIGHTS,

			LIGHT_COLOR,
			LIGHT_INTENSITY,
			LIGHT_RANGE,
			LIGHT_INNERCONE,
			LIGHT_OUTERCONE,
			// additional light paths can go here...
			_LIGHT_RANGE_END = LIGHT_COLOR + 1000,

			EMITTER_EMITCOUNT,
			// additional emitter paths can go here...
			_EMITTER_RANGE_END = EMITTER_EMITCOUNT + 1000,

			CAMERA_FOV,
			CAMERA_FOCAL_LENGTH,
			CAMERA_APERTURE_SIZE,
			CAMERA_APERTURE_SHAPE,
			// additional camera paths can go here...
			_CAMERA_RANGE_END = CAMERA_FOV + 1000,

			// future work: SCRIPT-binding...
			SCRIPT_PLAY,
			SCRIPT_STOP,
			// additional script paths can go here...
			_SCRIPT_RANGE_END = SCRIPT_PLAY + 1000,

			MATERIAL_COLOR,
			MATERIAL_EMISSIVE,
			MATERIAL_ROUGHNESS,
			MATERIAL_METALNESS,
			MATERIAL_REFLECTANCE,
			MATERIAL_TEXMULADD,
			// additional material paths can go here...
			_MATERIAL_RANGE_END = MATERIAL_COLOR + 1000,

			UNKNOWN,
		};
		
		// Key frame data structure
		struct KeyFrame
		{
			float time;					// Time in seconds
			vfloat4 value;				// Value (can be position, rotation quaternion, etc.)
			vfloat4 inTangent;			// Input tangent for spline interpolation
			vfloat4 outTangent;			// Output tangent for spline interpolation
			Interpolation interpolation;

			KeyFrame() : time(0.0f), value{ 0,0,0,0 }, inTangent{ 0,0,0,0 }, outTangent{ 0,0,0,0 }, interpolation(Interpolation::LINEAR) {}
			KeyFrame(float t, const vfloat3& val) : time(t), value{ val.x, val.y, val.z, 0.0f }, inTangent{ 0,0,0,0 }, outTangent{ 0,0,0,0 }, interpolation(Interpolation::LINEAR) {}
			KeyFrame(float t, const vfloat4& val) : time(t), value(val), inTangent{ 0,0,0,0 }, outTangent{ 0,0,0,0 }, interpolation(Interpolation::LINEAR) {}
		};
		struct CameraKeyFrame
		{
			vfloat3 pos;
			vfloat3 view;
			vfloat3 up;
		};

		VzAnimation(const VID vid, const std::string& originFrom)
			: VzBaseComp(vid, originFrom, COMPONENT_TYPE::ANIMATION) {}
		virtual ~VzAnimation() = default;

		void Play();
		void Pause();
		void Stop();
		void Reset();
		void SetTime(float time);
		float GetTime() const;
		float GetDuration() const;

		void SetSpeed(float speed);
		float GetSpeed() const;
		void SetLoopMode(LoopMode mode);
		LoopMode GetLoopMode() const;

		bool IsPlaying() const;
		bool IsPaused() const;
		bool IsFinished() const;

		struct Channel
		{
			void AddKeyFrame(const KeyFrame& keyFrame);
			void AddKeyFrame(float time, const vfloat3& value, Interpolation interpolation = Interpolation::LINEAR);
			void AddKeyFrame(float time, const vfloat4& value, Interpolation interpolation = Interpolation::LINEAR);
			void RemoveKeyFrame(int index);
			void ClearKeyFrames();

			size_t GetKeyFrameCount() const;
			KeyFrame GetKeyFrame(int index) const;
			std::vector<KeyFrame> GetKeyFrames() const;

			void SetTargetObject(VID targetObjectVID);
			VID GetTargetObject() const;

			Path GetPath() const;
			void SetPath(Path path);

			void SetChannelName(const std::string& channelName);
			std::string GetChannelName() const;

			float GetDuration() const;
			vfloat4 EvaluateAt(float time) const;
			void SortKeyFramesByTime();
		};

		int AddChannel(const Path path, const std::string& name, const VID targetVID = INVALID_VID);
		void RemoveChannel(const int index);
		void RemoveChannel(const Channel& channel);
		void RemoveChannel(const std::string& name);
		void ClearChannels();
		
		size_t GetChannelCount() const;
		const Channel& GetChennel(int index) const;
		const Channel& GetChennel(const Channel& channel) const;
		const Channel& GetChennel(const std::string& name) const;
		const std::vector<Channel*> GetTracksForTarget(VID targetVID) const;

		// Object animation utilities
		Channel* AddObjectPositionTrack(const ActorVID actorVID);
		Channel* AddObjectRotationTrack(const ActorVID actorVID);
		Channel* AddObjectScaleTrack(const ActorVID actorVID);
		
		// Batch operations for common animation patterns
		void CreateCameraFlythrough(const CamVID cameraVID, const std::vector<CameraKeyFrame>& keyFrames);
		void CreateCameraOrbit(const CamVID cameraVID, const vfloat3& center, float radius, float duration, int numRevolutions = 1);
		void CreateCameraZoom(const CamVID cameraVID, float startFOV, float endFOV, float duration);
		
		// Callbacks (delegates to AnimationDataComponent)
		void SetOnCompleteCallback(std::function<void(VzAnimation*)> callback);
		void SetOnUpdateCallback(std::function<void(VzAnimation*, float)> callback);

		// Update function (called by engine - delegates to AnimationComponent)
		void Update(float deltaTime);
		
		// Utility functions (delegates to AnimationComponent)
		void CalculateDuration();
		void ApplyAnimationToTargets();
	};

	// Animation system utilities
	namespace AnimationUtils
	{
		using LoopMode = VzAnimation::LoopMode;
		// Interpolation functions
		API_EXPORT float LinearInterpolate(float a, float b, float t);
		API_EXPORT vfloat3 LinearInterpolate(const vfloat3& a, const vfloat3& b, float t);
		API_EXPORT vfloat4 LinearInterpolate(const vfloat4& a, const vfloat4& b, float t);
		
		API_EXPORT vfloat4 QuaternionSlerp(const vfloat4& a, const vfloat4& b, float t);
		API_EXPORT vfloat4 CubicSplineInterpolate(const vfloat4& p0, const vfloat4& p1, const vfloat4& p2, const vfloat4& p3, float t);
		
		// Camera utilities
		API_EXPORT vfloat4 LookRotationToQuaternion(const vfloat3& viewDir, const vfloat3& up);
		API_EXPORT void QuaternionToLookRotation(const vfloat4& quaternion, vfloat3& viewDir, vfloat3& up);
		
		// Time utilities
		API_EXPORT float WrapTime(float time, float duration, LoopMode loopMode);
		
		// Easing functions
		API_EXPORT float EaseInOut(float t);
		API_EXPORT float EaseIn(float t);
		API_EXPORT float EaseOut(float t);
	}
}
